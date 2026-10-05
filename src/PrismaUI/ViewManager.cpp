#include "ViewManager.h"

#include "Cef/Browser/CefRuntime.h"
#include "Core.h"
#include "Globals.h"
#include "InputHandler.h"
#include "Menus/PrismaUIMenu.h"

namespace PrismaUI::ViewManager {
    using namespace Core;

    namespace {
        // Vanilla cancels a held attack/cast through PlayerControls' menu-mode transition, not through the
        // button release: the release never reaches AttackBlockHandler because no menu-mode input context
        // maps the attack buttons (see Interface/Controls/PC/controlmap.txt), so CanProcess rejects it.
        // Instead, entering menu mode arms triggerReleaseEvent on every held-state handler, and leaving it
        // makes the next PlayerControls::ProcessEvent(InputEvent**) synthesize a "ForceRelease" ButtonEvent
        // for the armed handlers, which is what actually stops the cast. Verified by disassembling
        // SkyrimSE 1.5.97: the PlayerControls MenuModeChangeEvent sink branches into sub_140705960
        // (arm) / flags data+0x2A, consumed as sub_1407059F0 (broadcast) from the input sink.
        //
        // PrismaUIMenu is kAlwaysOpen and only becomes menu-mode-hungry on focus, so the engine never
        // raises that event for us and the latch is never cleared - the player keeps casting after
        // releasing the mouse inside a focused view. Drive the same transition on the main thread; the
        // sink only reads the mode, but pass the real menu name anyway.
        void NotifyPlayerControlsMenuMode(RE::MenuModeChangeEvent::Mode mode) {
            MainThreadScheduler.Post([mode] {
                auto* controls = RE::PlayerControls::GetSingleton();
                if (!controls) {
                    return;
                }

                RE::MenuModeChangeEvent event{};
                event.menu = Menus::PrismaUIMenu::MENU_NAME;
                event.mode = mode;
                static_cast<RE::BSTEventSink<RE::MenuModeChangeEvent>*>(controls)->ProcessEvent(&event, nullptr);
            });
        }

        // Apply the native side-effects of focusing a Prisma view (control-map disable,
        // FocusMenu open, optional pause, input capture). Caller already holds the target
        // viewData and has cleared focus on any other focused views.
        void ApplyFocusSideEffects(PrismaViewId viewId, const std::shared_ptr<PrismaView>& viewData, bool pauseGame) {
            InputHandler::GetSingleton().EnableInputCapture(viewId);

            Menus::PrismaUIMenu::Focus();
            NotifyPlayerControlsMenuMode(RE::MenuModeChangeEvent::Mode::kDisplayed);

            if (auto* controlMap = RE::ControlMap::GetSingleton()) {
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kWheelZoom, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kLooking, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kJumping, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kMovement, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kActivate, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch, false, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kVATS, false, false);
            }

            if (pauseGame) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    ui->numPausesGame++;
                    viewData->isPaused.store(true);
                    logger::info("Focus: View [{}] paused the game.", viewId);
                }
            }
        }

        // Apply the native side-effects of unfocusing: blur the iframe via CEF, restore
        // controls, optionally close FocusMenu, drop the pause counter. closeFocusMenu==false
        // is used when transferring focus between Prisma views so the focus surface stays open.
        void ApplyUnfocusSideEffects(PrismaViewId viewId, const std::shared_ptr<PrismaView>& viewData) {
            if (viewData->isPaused.load()) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    if (ui->numPausesGame > 0) {
                        ui->numPausesGame--;
                    }
                }
                viewData->isPaused.store(false);
                logger::info("Unfocus: View [{}] released the pause counter.", viewId);
            }

            InputHandler::GetSingleton().DisableInputCapture(viewId);
            InputHandler::GetSingleton().ClearImeState(viewId);

            Cef::CefRuntime::GetSingleton().BlurShellView(viewId);
            viewData->isFocused.store(false);

            Menus::PrismaUIMenu::Unfocus();
            NotifyPlayerControlsMenuMode(RE::MenuModeChangeEvent::Mode::kHidden);

            if (auto* controlMap = RE::ControlMap::GetSingleton()) {
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kWheelZoom, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kLooking, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kJumping, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kMovement, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kActivate, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch, true, false);
                controlMap->ToggleControls(RE::UserEvents::USER_EVENT_FLAG::kVATS, true, false);
            }
        }
    }  // namespace

    PrismaViewId Create(const std::string& htmlPath, std::function<void(PrismaViewId)> onDomReadyCallback) {
        const PrismaViewId newViewId = nextViewId.fetch_add(1, std::memory_order_relaxed);

        // Mirror CefRuntime's URL resolution shape for logging consistency; the runtime
        // re-resolves the same way when CreateShellView is dispatched.
        std::string resolvedUrl;
        if (htmlPath.rfind("http://", 0) == 0 || htmlPath.rfind("https://", 0) == 0) {
            resolvedUrl = htmlPath;
        } else {
            resolvedUrl = "file:///views/" + htmlPath;
        }

        auto viewData = std::make_shared<PrismaView>();
        viewData->id = newViewId;
        viewData->iframeName = std::to_string(newViewId);
        viewData->resolvedUrl = resolvedUrl;
        viewData->originalUrl = resolvedUrl;  // retained for Step 11 recovery policy
        viewData->isHidden = false;
        viewData->domReadyCallback = std::move(onDomReadyCallback);

        {
            auto viewsLock = views.Acquire();
            int maxOrder = -1;
            for (const auto& val : *viewsLock | std::views::values) {
                if (val && val->order > maxOrder) {
                    maxOrder = val->order;
                }
            }
            viewData->order = maxOrder + 1;
            (*viewsLock)[newViewId] = viewData;
        }

        logger::info("View [{}] create requested: iframe={}, url={}, order={}", newViewId, viewData->iframeName,
                     resolvedUrl, viewData->order);

        // Enqueue the actual CEF shell command. CefRuntime caches the request and replays it
        // once the shell page is ready, so we deliberately do not block on shell readiness.
        const std::string htmlPathCopy = htmlPath;
        const int order = viewData->order;
        bool expected = false;
        if (!viewData->iframeCreateRequested.compare_exchange_strong(expected, true)) {
            logger::debug("Create: View [{}] iframe already requested; skipping duplicate.", newViewId);
            return newViewId;
        }

        const bool ok =
            Cef::CefRuntime::GetSingleton().CreateShellView(newViewId, htmlPathCopy, order, /*hidden=*/false);
        if (!ok) {
            logger::warn(
                "Create: CefRuntime::CreateShellView returned false for View [{}] (iframe={}); shell replay "
                "should attach it once the CEF browser is ready.",
                newViewId, viewData->iframeName);
        } else {
            logger::info("Create: View [{}] iframe={} dispatched to CEF shell.", newViewId, viewData->iframeName);
        }
        if (viewData->isFocused.load()) {
            Cef::CefRuntime::GetSingleton().FocusShellView(newViewId);
        }

        return newViewId;
    }

    std::shared_ptr<PrismaView> LookupView(PrismaViewId viewId) {
        auto viewsLock = views.Acquire();
        auto it = viewsLock->find(viewId);
        if (it == viewsLock->end()) return nullptr;
        return it->second;
    }

    void Show(PrismaViewId viewId) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("Show: View ID [{}] not found.", viewId);
            return;
        }

        if (!viewData->isHidden.load()) {
            logger::debug("Show: View [{}] is already visible.", viewId);
            return;
        }
        viewData->isHidden.store(false);
        Cef::CefRuntime::GetSingleton().SetShellViewHidden(viewId, false);
        if (viewData->isFocused.load()) {
            Cef::CefRuntime::GetSingleton().FocusShellView(viewId);
        }
        logger::info("Show: View [{}] (iframe={}) marked visible.", viewId, viewData->iframeName);
    }

    void Hide(PrismaViewId viewId) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("Hide: View ID [{}] not found.", viewId);
            return;
        }

        if (viewData->isHidden.load()) {
            logger::debug("Hide: View [{}] is already hidden.", viewId);
            return;
        }

        if (viewData->isFocused.load()) {
            ApplyUnfocusSideEffects(viewId, viewData);
            logger::info("Hide: View [{}] was focused; unfocused before hiding.", viewId);
        }

        viewData->isHidden.store(true);
        Cef::CefRuntime::GetSingleton().SetShellViewHidden(viewId, true);
        logger::info("Hide: View [{}] (iframe={}) marked hidden.", viewId, viewData->iframeName);
    }

    bool IsHidden(PrismaViewId viewId) {
        auto viewsLock = views.Acquire();
        auto it = viewsLock->find(viewId);
        if (it != viewsLock->end()) {
            return it->second->isHidden.load();
        }
        logger::warn("IsHidden: View ID [{}] not found.", viewId);
        return true;
    }

    bool IsValid(PrismaViewId viewId) {
        auto viewsLock = views.Acquire();
        return viewsLock->contains(viewId);
    }

    bool Focus(PrismaViewId viewId, bool pauseGame) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("Focus: View ID [{}] not found.", viewId);
            return false;
        }

        if (viewData->isHidden.load()) {
            logger::warn("Focus: View [{}] is hidden; cannot focus.", viewId);
            return false;
        }

        if (viewData->isFocused.load()) {
            logger::debug("Focus: View [{}] already focused.", viewId);
            return true;
        }

        // Queue unfocus on every other currently-focused view. We pass closeFocusMenu=false
        // so the focus surface stays open during transfer.
        std::vector<PrismaViewId> viewsToUnfocus;
        {
            auto viewsLock = views.Acquire();
            for (const auto& pair : *viewsLock) {
                if (pair.first != viewId && pair.second && pair.second->isFocused.load()) {
                    viewsToUnfocus.push_back(pair.first);
                }
            }
        }

        for (const auto& idToUnfocus : viewsToUnfocus) {
            auto vd = LookupView(idToUnfocus);
            if (!vd) continue;
            if (!vd->isFocused.load()) continue;
            ApplyUnfocusSideEffects(idToUnfocus, vd);
            logger::info("Focus: View [{}] unfocused (focus switching).", idToUnfocus);
        }

        viewData->isFocused.store(true);
        Cef::CefRuntime::GetSingleton().FocusShellView(viewId);
        ApplyFocusSideEffects(viewId, viewData, pauseGame);

        logger::info("Focus: View [{}] (iframe={}) focused: pauseGame={}", viewId, viewData->iframeName, pauseGame);

        return true;
    }

    void Unfocus(PrismaViewId viewId) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("Unfocus: View ID [{}] not found.", viewId);
            InputHandler::GetSingleton().DisableInputCapture(0);
            Menus::PrismaUIMenu::Unfocus();
            return;
        }

        if (!viewData->isFocused.load()) {
            logger::debug("Unfocus: View [{}] was not focused.", viewId);
            return;
        }

        ApplyUnfocusSideEffects(viewId, viewData);
        logger::info("Unfocus: View [{}] (iframe={}) unfocused.", viewId, viewData->iframeName);
    }

    bool HasFocus(PrismaViewId viewId) {
        auto viewsLock = views.Acquire();
        auto it = viewsLock->find(viewId);
        if (it == viewsLock->end()) {
            logger::warn("HasFocus: View ID [{}] not found.", viewId);
            return false;
        }

        return it->second->isFocused.load();
    }

    bool ViewHasInputFocus(PrismaViewId viewId) {
        auto viewsLock = views.Acquire();
        auto it = viewsLock->find(viewId);
        if (it == viewsLock->end()) return false;
        return it->second->isFocused.load();
    }

    void SetScrollingPixelSize(PrismaViewId viewId, int pixelSize) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("SetScrollingPixelSize: View ID [{}] not found.", viewId);
            return;
        }
        if (pixelSize <= 0) {
            logger::warn("SetScrollingPixelSize: Invalid pixel size {} for view [{}]. Must be > 0. Using default.",
                         pixelSize, viewId);
            viewData->scrollingPixelSize = 16;
        } else {
            viewData->scrollingPixelSize = pixelSize;
            logger::debug("SetScrollingPixelSize: Set {} pixels per scroll line for view [{}]", pixelSize, viewId);
        }
    }

    int GetScrollingPixelSize(PrismaViewId viewId) {
        auto viewData = LookupView(viewId);
        if (viewData) {
            return viewData->scrollingPixelSize;
        }

        logger::warn("GetScrollingPixelSize: View ID [{}] not found, returning default.", viewId);
        return 28;
    }

    void Destroy(PrismaViewId viewId) {
        logger::info("Destroy: Beginning destruction of View [{}]", viewId);

        std::shared_ptr<PrismaView> viewDataToDestroy;
        {
            auto viewsLock = views.Acquire();
            auto it = viewsLock->find(viewId);
            if (it == viewsLock->end()) {
                logger::warn("Destroy: View ID [{}] not found.", viewId);
                return;
            }

            viewDataToDestroy = std::move(it->second);
            viewsLock->erase(it);
        }

        viewDataToDestroy->destroyRequested.store(true, std::memory_order_release);

        // If the view was focused, run the unfocus side-effects inline. The operation queue
        // is already drained, and the view has been removed from the map so no peer can race.
        if (viewDataToDestroy->isFocused.load()) {
            ApplyUnfocusSideEffects(viewId, viewDataToDestroy);
            logger::info("Destroy: View [{}] was focused; applied unfocus side-effects inline.", viewId);
        }

        viewDataToDestroy->isHidden.store(true);

        // Remove any JS callbacks registered for this view.
        {
            auto jsCallbacksLock = jsCallbacks.Acquire();
            std::size_t removed = 0;
            for (auto it = jsCallbacksLock->begin(); it != jsCallbacksLock->end();) {
                if (it->first.first == viewId) {
                    it = jsCallbacksLock->erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }

            if (removed > 0) {
                logger::debug("Destroy: Removed {} JavaScript callback(s) for View [{}]", removed, viewId);
            }
        }

        // Drain any in-flight Invoke callbacks so the caller gets one final empty
        // string instead of being left holding a never-fired callback.
        Cef::CefRuntime::GetSingleton().CancelInvokesForView(viewId);

        Cef::CefRuntime::GetSingleton().DestroyShellView(viewId);

        logger::info("Destroy: View [{}] (iframe={}) destroyed.", viewId, viewDataToDestroy->iframeName);
    }

    void SetOrder(PrismaViewId viewId, int order) {
        auto viewData = LookupView(viewId);
        if (!viewData) {
            logger::warn("SetOrder: View ID [{}] not found.", viewId);
            return;
        }

        viewData->order = order;

        Cef::CefRuntime::GetSingleton().SetShellViewOrder(viewId, order);
        if (viewData->isFocused.load()) {
            Cef::CefRuntime::GetSingleton().FocusShellView(viewId);
        }

        logger::info("SetOrder: View [{}] (iframe={}) order set to {}.", viewId, viewData->iframeName, order);
    }

    int GetOrder(PrismaViewId viewId) {
        auto viewData = LookupView(viewId);
        if (viewData) {
            return viewData->order;
        }

        logger::warn("GetOrder: View ID [{}] not found, returning -1.", viewId);
        return -1;
    }

    bool HasAnyActiveFocus() {
        auto viewsLock = views.Acquire();
        for (const auto& val : *viewsLock | std::views::values) {
            if (val && val->isFocused.load()) {
                return true;
            }
        }

        return false;
    }

    void RegisterConsoleCallback(
        PrismaViewId viewId,
        std::function<void(PrismaViewId, PRISMA_UI_API::ConsoleMessageLevel, const std::string&)> callback) {
        auto viewData = LookupView(viewId);
        if (viewData) {
            viewData->consoleMessageCallback = std::move(callback);
        } else {
            logger::warn("RegisterConsoleCallback: View ID [{}] not found.", viewId);
        }
    }

    void Shutdown() {
        logger::info("Shutdown...");

        std::vector<PrismaViewId> viewIdsToDestroy;
        {
            auto viewsLock = views.Acquire();
            for (const auto& pair : *viewsLock) {
                viewIdsToDestroy.push_back(pair.first);
                if (pair.second) {
                    // Mark each view as destroyRequested so any in-flight queue entries
                    // observe it and no-op before reaching CEF or render state (Step 6).
                    pair.second->destroyRequested.store(true, std::memory_order_release);
                }
            }
        }

        for (const auto& id : viewIdsToDestroy) {
            try {
                Destroy(id);
            } catch (const std::exception& e) {
                logger::error("Error destroying view [{}] during shutdown: {}", id, e.what());
            }
        }

        views.Acquire()->clear();

        logger::info("Shutdown complete");
    }
}
