#include "Communication.h"

#include "Cef/Browser/CefRuntime.h"
#include "Core.h"
#include "PrismaUI_API.h"
#include "ViewManager.h"

namespace PrismaUI::Communication {
    using namespace Core;

    namespace {
        PRISMA_UI_API::ConsoleMessageLevel ParseConsoleLevel(const std::string& level) {
            if (level == "warn" || level == "warning") return PRISMA_UI_API::ConsoleMessageLevel::Warning;
            if (level == "error") return PRISMA_UI_API::ConsoleMessageLevel::Error;
            if (level == "debug") return PRISMA_UI_API::ConsoleMessageLevel::Debug;
            if (level == "info") return PRISMA_UI_API::ConsoleMessageLevel::Info;
            return PRISMA_UI_API::ConsoleMessageLevel::Log;
        }
    }

    void Invoke(Core::PrismaViewId viewId, std::string script, std::function<void(std::string)> callback) {
        if (!ViewManager::IsValid(viewId)) {
            logger::warn("Invoke: View ID [{}] not found.", viewId);
            if (callback) callback(std::string());
            return;
        }

        Cef::CefRuntime::GetSingleton().InvokeScript(viewId, std::move(script), std::move(callback));
    }

    void RegisterJSListener(Core::PrismaViewId viewId, const std::string& name, Core::SimpleJSCallback callback) {
        if (!ViewManager::IsValid(viewId)) {
            logger::error("RegisterJSListener: View ID [{}] not found.", viewId);
            return;
        }

        {
            auto jsCallbacksLock = jsCallbacks.Acquire();
            JSCallbackData data;
            data.viewId = viewId;
            data.name = name;
            data.callback = std::move(callback);
            (*jsCallbacksLock)[std::make_pair(viewId, name)] = std::move(data);
            logger::info("RegisterJSListener: stored callback '{}' for view [{}]", name, viewId);
        }

        // Tell the renderer subprocess to install (or refresh) the window[name] trampoline.
        // If the iframe isn't attached yet, CefRuntime logs and the renderer will install
        // it lazily on the next OnContextCreated for that iframe.
        Cef::CefRuntime::GetSingleton().RegisterListener(viewId, name, /*unused*/ nullptr);
    }

    void InteropCall(Core::PrismaViewId viewId, const std::string& functionName, const std::string& argument) {
        if (!ViewManager::IsValid(viewId)) {
            logger::warn("InteropCall: View ID [{}] not found.", viewId);
            return;
        }

        Cef::CefRuntime::GetSingleton().InteropCallInView(viewId, functionName, argument);
    }

    // -------------------------------------------------------------------------
    // Dispatch helpers
    // -------------------------------------------------------------------------

    void DispatchListenerInvoke(uint64_t viewId, const std::string& name, std::string argument) {
        Core::SimpleJSCallback target;
        {
            auto jsCallbacksLock = jsCallbacks.Acquire();
            auto it = jsCallbacksLock->find(std::make_pair(static_cast<Core::PrismaViewId>(viewId), name));
            if (it != jsCallbacksLock->end()) {
                target = it->second.callback;
            }
        }

        if (!target) {
            logger::warn("DispatchListenerInvoke: no callback registered for view [{}] / '{}'.", viewId, name);
            return;
        }
        try {
            target(argument);
        } catch (const std::exception& e) {
            logger::error("DispatchListenerInvoke: callback for view [{}] / '{}' threw: {}", viewId, name, e.what());
        } catch (...) {
            logger::error("DispatchListenerInvoke: callback for view [{}] / '{}' threw an unknown exception.", viewId,
                          name);
        }
    }

    void DispatchConsoleMessage(uint64_t viewId, const std::string& level, std::string text) {
        auto viewData = ViewManager::LookupView(viewId);
        if (!viewData || !viewData->consoleMessageCallback) {
            return;
        }

        const auto resolvedLevel = ParseConsoleLevel(level);
        try {
            viewData->consoleMessageCallback(static_cast<Core::PrismaViewId>(viewId), resolvedLevel, text);
        } catch (const std::exception& e) {
            logger::error("DispatchConsoleMessage: callback for view [{}] threw: {}", viewId, e.what());
        } catch (...) {
            logger::error("DispatchConsoleMessage: callback for view [{}] threw an unknown exception.", viewId);
        }
    }

    void DispatchDomReady(uint64_t viewId) {
        auto viewData = ViewManager::LookupView(viewId);
        if (!viewData) {
            logger::warn("DispatchDomReady: view [{}] not found.", viewId);
            return;
        }

        viewData->iframeReady.store(true, std::memory_order_release);
        logger::info("DispatchDomReady: view [{}] iframe DOM ready.", viewId);

        std::vector<std::string> listenerNames;
        {
            auto jsCallbacksLock = jsCallbacks.Acquire();
            for (const auto& [key, data] : *jsCallbacksLock) {
                if (key.first == viewId) {
                    listenerNames.push_back(data.name);
                }
            }
        }

        for (const auto& name : listenerNames) {
            Cef::CefRuntime::GetSingleton().RegisterListener(viewId, name, /*unused*/ nullptr);
        }

        if (viewData->domReadyCallback) {
            try {
                viewData->domReadyCallback(static_cast<Core::PrismaViewId>(viewId));
            } catch (const std::exception& e) {
                logger::error("DispatchDomReady: callback for view [{}] threw: {}", viewId, e.what());
            } catch (...) {
                logger::error("DispatchDomReady: callback for view [{}] threw an unknown exception.", viewId);
            }
        }
    }
}
