#include <windows.h>

#include "Cef/Subprocess/PrismaCefApp.h"
#include "include/cef_app.h"

namespace {
    // Windows applies EcoQoS/power throttling to window-less background processes. Opt out so the
    // CEF renderer (and other helpers) keep full CPU speed while the game is in the foreground.
    void DisablePowerThrottling() {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = 0;
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    }

    int RunCefSubprocess(HINSTANCE instance) {
        DisablePowerThrottling();
        CefMainArgs mainArgs(instance);
        CefRefPtr<CefApp> app = PrismaUI::Cef::CreatePrismaCefApp();

        const int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
        return exitCode >= 0 ? exitCode : 0;
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) { return RunCefSubprocess(instance); }
