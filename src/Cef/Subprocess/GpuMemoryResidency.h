#pragma once

#include <windows.h>

#include <cstdint>
#include <thread>

namespace PrismaUI::Cef {
    // Keeps the CEF GPU process' local (dedicated) video memory working set resident.
    //
    // Skyrim owns the foreground window, so Windows' video memory manager shrinks the
    // GPU process' budget first and demotes its allocations - large 2D canvas caches
    // above all - into system memory, which then has to be pulled back across PCIe.
    // Windows exposes no way to raise another process' residency priority, but the CEF
    // GPU process is PrismaUICefSubprocess.exe, so the in-process hint
    // IDXGIAdapter3::SetVideoMemoryReservation is ours to call.
    //
    // The reservation tracks measured local-segment usage instead of a fixed number:
    // it is a minimum-working-set hint that does not raise the budget, so reserving
    // more than the process uses would only push the stutter back into Skyrim. The
    // target is clamped by DXGI's AvailableForReservation and by
    // PRISMAUI_GPU_VRAM_RESERVATION_MB (megabytes; 0 disables the watcher).
    //
    // Construction is a no-op in every process that is not "--type=gpu-process".
    class GpuMemoryResidency {
    public:
        GpuMemoryResidency();
        ~GpuMemoryResidency();

        GpuMemoryResidency(const GpuMemoryResidency&) = delete;
        GpuMemoryResidency& operator=(const GpuMemoryResidency&) = delete;

    private:
        void Watch(LUID adapterLuid, std::uint64_t reservationCapBytes);

        HANDLE _stopEvent = nullptr;
        std::thread _worker;
    };
}
