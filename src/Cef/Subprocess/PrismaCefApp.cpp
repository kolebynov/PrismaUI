#include "Cef/Subprocess/PrismaCefApp.h"

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
    }

    CefRefPtr<CefRenderProcessHandler> PrismaCefApp::GetRenderProcessHandler() { return renderHandler_; }

    CefRefPtr<CefApp> CreatePrismaCefApp(std::string adapterLuidValue) {
        return new PrismaCefApp(std::move(adapterLuidValue));
    }
}
