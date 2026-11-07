#include "Cef/Subprocess/PrismaCefApp.h"

#include <string>
#include <string_view>
#include <utility>

#include "Cef/Subprocess/PrismaCefRenderApp.h"
#include "include/cef_command_line.h"

namespace PrismaUI::Cef {
    PrismaCefApp::PrismaCefApp(std::string adapterLuidValue)
        : renderHandler_(new PrismaCefRenderApp()), adapterLuidValue_(std::move(adapterLuidValue)) {}

    void PrismaCefApp::OnBeforeCommandLineProcessing(const CefString&, CefRefPtr<CefCommandLine> command_line) {
        if (!command_line) {
            return;
        }

        // CEF/Chromium M138+ auto de-elevates when launched as admin: it relaunches the main
        // executable de-elevated and CefInitialize returns false (CefGetExitCode == 38,
        // CEF_RESULT_CODE_NORMAL_EXIT_AUTO_DE_ELEVATED). Inside SkyrimSE.exe that would try to
        // relaunch the game itself, ignoring browser_subprocess_path. Disable it so PrismaUI
        // initializes normally when the game/mod manager runs elevated.
        command_line->AppendSwitch("do-not-de-elevate");

        command_line->AppendSwitch("disable-smooth-scrolling");
        command_line->AppendSwitch("allow-file-access-from-files");
        command_line->AppendSwitch("allow-universal-access-from-files");
        command_line->AppendSwitchWithValue("use-gl", "angle");
        command_line->AppendSwitchWithValue("use-angle", "d3d11");

        // Run the GPU thread (and ANGLE's D3D11 device) inside SkyrimSE.exe so WDDM schedules it
        // with the game's foreground priority instead of as a background helper process. A
        // GPU-thread crash now takes down the game, and the GPU cannot be restarted. Requires the
        // CEF UI loop to run on the thread that called CefInitialize (see CefRuntime::Initialize):
        // with multi_threaded_message_loop CEF 147 fails a thread_checker DCHECK at startup.
        command_line->AppendSwitch("in-process-gpu");

        // Chromium otherwise lowers the renderer's process priority (and on Windows 11 applies
        // EcoQoS) whenever it considers the renderer backgrounded.
        command_line->AppendSwitch("disable-renderer-backgrounding");

        // Force Chromium's GPU process onto the same D3D11 adapter Skyrim renders on. On
        // hybrid-GPU machines Chromium otherwise composites OSR output on a different
        // adapter, so OnAcceleratedPaint's shared NT texture cannot be opened on our
        // render device (OpenSharedResource1 fails with E_INVALIDARG) and nothing draws.
        // Value is "<HighPart>,<LowPart>" decimal; ANGLE reads it on the in-process GPU thread.
        if (!adapterLuidValue_.empty()) {
            command_line->AppendSwitchWithValue("use-adapter-luid", adapterLuidValue_);
        }

        DisableUnusedFeatures(command_line);
    }

    namespace {
        // Chromium honors only the last value of a comma-list switch such as --disable-features, so
        // merge with whatever CEF or the user already passed.
        void AppendListSwitch(const CefRefPtr<CefCommandLine>& command_line, const char* name,
                              std::string_view values) {
            std::string merged = command_line->GetSwitchValue(name).ToString();
            if (!merged.empty()) {
                merged.push_back(',');
            }
            merged.append(values);
            command_line->AppendSwitchWithValue(name, merged);
        }
    }

    void PrismaCefApp::DisableUnusedFeatures(const CefRefPtr<CefCommandLine>& command_line) {
        // Background services that would otherwise phone home or start threads inside the game.
        command_line->AppendSwitch("disable-background-networking");
        command_line->AppendSwitch("disable-component-update");
        command_line->AppendSwitch("disable-extensions");
        command_line->AppendSwitch("disable-speech-api");
        command_line->AppendSwitch("disable-sync");
        command_line->AppendSwitch("disable-default-apps");
        command_line->AppendSwitch("disable-domain-reliability");
        command_line->AppendSwitch("no-first-run");

        // WebRTC is compiled into libcef and has no runtime kill switch (deleting the JS globals in
        // OnContextCreated does not stick: Blink reinstalls RTCDataChannel before page scripts run).
        // Forbid every non-proxied UDP socket so peer connections cannot be established and local
        // IPs never leak; camera/microphone access is denied by CefOsrClient's permission handler.
        command_line->AppendSwitchWithValue("webrtc-ip-handling-policy", "disable_non_proxied_udp");

        // HardwareMediaKeyHandling: Chromium otherwise grabs the system Play/Pause keys from the
        // user's media player whenever a view plays audio. The rest are Cast discovery and
        // Google service clients.
        AppendListSwitch(command_line, "disable-features",
                         "HardwareMediaKeyHandling,GlobalMediaControls,MediaRouter,DialMediaRouteProvider,Translate,"
                         "OptimizationHints,AutofillServerCommunication");
    }

    CefRefPtr<CefRenderProcessHandler> PrismaCefApp::GetRenderProcessHandler() { return renderHandler_; }

    CefRefPtr<CefApp> CreatePrismaCefApp(std::string adapterLuidValue) {
        return new PrismaCefApp(std::move(adapterLuidValue));
    }
}
