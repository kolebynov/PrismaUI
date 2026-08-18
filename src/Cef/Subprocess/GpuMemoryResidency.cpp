#include "Cef/Subprocess/GpuMemoryResidency.h"

#include <dxgi1_4.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <ios>
#include <string>
#include <string_view>

#include "include/base/cef_logging.h"

namespace PrismaUI::Cef {
    namespace {
        // content::switches::kProcessType / kGpuProcess and gl::switches::kUseAdapterLuid.
        // GpuProcessHost::LaunchGpuProcess copies --use-adapter-luid into the GPU child and
        // ANGLE resolves its D3D11 device from it, so the switch names exactly the adapter
        // whose residency matters here. Chromium's format is "<high part>,<low part>", with
        // a signed high part and an unsigned low part.
        constexpr std::wstring_view kProcessTypeSwitchPrefix = L"--type=";
        constexpr std::wstring_view kGpuProcessType = L"gpu-process";
        constexpr std::wstring_view kAdapterLuidSwitchPrefix = L"--use-adapter-luid=";

        constexpr wchar_t kReservationCapEnvVar[] = L"PRISMAUI_GPU_VRAM_RESERVATION_MB";
        constexpr std::uint64_t kMib = 1024ull * 1024ull;
        constexpr std::uint64_t kDefaultReservationCapMib = 1024;

        // Reservations only move in quanta, so a busy compositor cannot re-arm the kernel
        // hint on every tick; rounding up also buys headroom for growth between ticks.
        constexpr std::uint64_t kReservationQuantumBytes = 32 * kMib;

        // Budget changes are event-driven, usage growth is not, hence the wait timeout.
        constexpr DWORD kPollIntervalMs = 2000;

        struct GpuProcessSwitches {
            bool isGpuProcess = false;
            bool hasAdapterLuid = false;
            LUID adapterLuid{};
        };

        // Returns an empty string for anything that is not printable ASCII, which is all
        // the switch values parsed here are ever allowed to be.
        std::string ToAscii(std::wstring_view value) {
            std::string ascii;
            ascii.reserve(value.size());
            for (const wchar_t character : value) {
                if (character <= 0 || character > 0x7F) {
                    return {};
                }

                ascii.push_back(static_cast<char>(character));
            }

            return ascii;
        }

        template <typename T>
        bool ParseDecimal(std::string_view text, T& value) {
            const char* const last = text.data() + text.size();
            const auto result = std::from_chars(text.data(), last, value);
            return result.ec == std::errc{} && result.ptr == last;
        }

        bool ParseAdapterLuid(std::wstring_view value, LUID& luid) {
            const std::string ascii = ToAscii(value);
            const size_t comma = ascii.find(',');
            if (comma == std::string::npos) {
                return false;
            }

            std::int32_t highPart = 0;
            std::uint32_t lowPart = 0;
            const std::string_view text(ascii);
            if (!ParseDecimal(text.substr(0, comma), highPart) || !ParseDecimal(text.substr(comma + 1), lowPart)) {
                return false;
            }

            luid.HighPart = highPart;
            luid.LowPart = lowPart;
            return true;
        }

        GpuProcessSwitches ReadGpuProcessSwitches() {
            GpuProcessSwitches result;

            int argc = 0;
            wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (!argv) {
                return result;
            }

            for (int i = 1; i < argc; ++i) {
                const std::wstring_view argument(argv[i]);
                if (argument.starts_with(kProcessTypeSwitchPrefix)) {
                    result.isGpuProcess = argument.substr(kProcessTypeSwitchPrefix.size()) == kGpuProcessType;
                } else if (argument.starts_with(kAdapterLuidSwitchPrefix)) {
                    result.hasAdapterLuid =
                        ParseAdapterLuid(argument.substr(kAdapterLuidSwitchPrefix.size()), result.adapterLuid);
                }
            }

            LocalFree(argv);
            return result;
        }

        std::uint64_t ReadReservationCapBytes() {
            wchar_t buffer[32]{};
            const DWORD length =
                GetEnvironmentVariableW(kReservationCapEnvVar, buffer, static_cast<DWORD>(std::size(buffer)));
            if (length == 0 || length >= std::size(buffer)) {
                return kDefaultReservationCapMib * kMib;
            }

            std::uint64_t megabytes = 0;
            if (!ParseDecimal(std::string_view(ToAscii(std::wstring_view(buffer, length))), megabytes)) {
                return kDefaultReservationCapMib * kMib;
            }

            return megabytes * kMib;
        }

        constexpr std::uint64_t RoundUpTo(std::uint64_t value, std::uint64_t quantum) {
            const std::uint64_t remainder = value % quantum;
            return remainder == 0 ? value : value + (quantum - remainder);
        }

        std::uint64_t ToMib(std::uint64_t bytes) { return bytes / kMib; }
    }

    GpuMemoryResidency::GpuMemoryResidency() {
        const GpuProcessSwitches switches = ReadGpuProcessSwitches();
        if (!switches.isGpuProcess) {
            return;
        }

        const std::uint64_t reservationCapBytes = ReadReservationCapBytes();
        if (reservationCapBytes == 0) {
            return;
        }

        if (!switches.hasAdapterLuid) {
            // CefRuntime could not pin the adapter, so ANGLE picked its own; reserving on a
            // guessed adapter would steal residency from whichever process owns it.
            LOG(WARNING) << "GpuMemoryResidency: no usable --use-adapter-luid, video memory reservation disabled.";
            return;
        }

        _stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!_stopEvent) {
            LOG(ERROR) << "GpuMemoryResidency: CreateEvent failed, error " << GetLastError() << ".";
            return;
        }

        _worker = std::thread([this, adapterLuid = switches.adapterLuid, reservationCapBytes] {
            Watch(adapterLuid, reservationCapBytes);
        });
    }

    GpuMemoryResidency::~GpuMemoryResidency() {
        if (_worker.joinable()) {
            SetEvent(_stopEvent);
            _worker.join();
        }

        if (_stopEvent) {
            CloseHandle(_stopEvent);
        }
    }

    void GpuMemoryResidency::Watch(LUID adapterLuid, std::uint64_t reservationCapBytes) {
        // The GPU process runs unsandboxed (CefSettings::no_sandbox), so DXGI is usable from
        // our own thread at any point. Enabling the sandbox would force this work to happen
        // before lockdown instead.
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) {
            LOG(ERROR) << "GpuMemoryResidency: CreateDXGIFactory1 failed. HR=0x" << std::hex
                       << static_cast<unsigned int>(hr) << std::dec;
            return;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> enumeratedAdapter;
        hr = factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(enumeratedAdapter.GetAddressOf()));
        if (FAILED(hr)) {
            LOG(ERROR) << "GpuMemoryResidency: EnumAdapterByLuid failed. HR=0x" << std::hex
                       << static_cast<unsigned int>(hr) << std::dec;
            return;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        hr = enumeratedAdapter.As(&adapter);
        if (FAILED(hr)) {
            LOG(ERROR) << "GpuMemoryResidency: adapter exposes no IDXGIAdapter3. HR=0x" << std::hex
                       << static_cast<unsigned int>(hr) << std::dec;
            return;
        }

        HANDLE budgetEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DWORD budgetCookie = 0;
        if (budgetEvent &&
            FAILED(adapter->RegisterVideoMemoryBudgetChangeNotificationEvent(budgetEvent, &budgetCookie))) {
            CloseHandle(budgetEvent);
            budgetEvent = nullptr;
        }

        const std::array<HANDLE, 2> waitHandles{_stopEvent, budgetEvent};
        const DWORD waitCount = budgetEvent ? 2u : 1u;

        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);
        LOG(INFO) << "GpuMemoryResidency: holding the CEF GPU process resident on adapter LUID " << adapterLuid.HighPart
                  << "," << adapterLuid.LowPart << " (" << ToMib(adapterDesc.DedicatedVideoMemory)
                  << " MiB dedicated), reservation cap " << ToMib(reservationCapBytes) << " MiB.";

        std::uint64_t reservedBytes = 0;
        bool failureLogged = false;

        for (;;) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo{};
            hr = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo);
            if (SUCCEEDED(hr)) {
                const std::uint64_t target = std::min({RoundUpTo(memoryInfo.CurrentUsage, kReservationQuantumBytes),
                                                       reservationCapBytes, memoryInfo.AvailableForReservation});
                if (target != reservedBytes) {
                    hr = adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, target);
                    if (SUCCEEDED(hr)) {
                        LOG(INFO) << "GpuMemoryResidency: reserved " << ToMib(target) << " MiB (usage "
                                  << ToMib(memoryInfo.CurrentUsage) << " MiB, budget " << ToMib(memoryInfo.Budget)
                                  << " MiB, reservable " << ToMib(memoryInfo.AvailableForReservation) << " MiB).";
                        reservedBytes = target;
                        failureLogged = false;
                    } else if (!failureLogged) {
                        LOG(WARNING) << "GpuMemoryResidency: SetVideoMemoryReservation(" << ToMib(target)
                                     << " MiB) failed. HR=0x" << std::hex << static_cast<unsigned int>(hr) << std::dec;
                        failureLogged = true;
                    }
                }
            } else if (!failureLogged) {
                LOG(WARNING) << "GpuMemoryResidency: QueryVideoMemoryInfo failed. HR=0x" << std::hex
                             << static_cast<unsigned int>(hr) << std::dec;
                failureLogged = true;
            }

            const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles.data(), FALSE, kPollIntervalMs);
            if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED) {
                break;
            }
        }

        if (budgetEvent) {
            adapter->UnregisterVideoMemoryBudgetChangeNotification(budgetCookie);
            CloseHandle(budgetEvent);
        }
    }
}
