#include <windows.h>

#include "Cef/Subprocess/GpuMemoryResidency.h"
#include "Cef/Subprocess/PrismaCefApp.h"
#include "include/cef_app.h"

namespace {
    int RunCefSubprocess(HINSTANCE instance) {
        // No-op outside the GPU process; see GpuMemoryResidency.h. Lives across
        // CefExecuteProcess so the watcher is torn down before the process exits.
        PrismaUI::Cef::GpuMemoryResidency gpuMemoryResidency;

        CefMainArgs mainArgs(instance);
        CefRefPtr<CefApp> app = PrismaUI::Cef::CreatePrismaCefApp();

        const int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
        return exitCode >= 0 ? exitCode : 0;
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) { return RunCefSubprocess(instance); }
