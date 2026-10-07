#pragma once

#include <Windows.h>

#include <bitset>
#include <map>
#include <memory>
#include <shared_mutex>

#include "Cef/Browser/CefRuntime.h"
#include "ImeHelper.h"

namespace PrismaUI::Core {
    typedef uint64_t PrismaViewId;
    struct PrismaView;
}

namespace PrismaUI {
    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler& GetSingleton();

        bool Initialize(HWND gameHwnd,
                        ResourceLock<std::map<Core::PrismaViewId, std::shared_ptr<Core::PrismaView>>>* viewsMap);

        void EnableInputCapture(Core::PrismaViewId viewId);
        void DisableInputCapture(Core::PrismaViewId viewId);
        void ClearImeState(Core::PrismaViewId viewId);
        bool IsAnyInputCaptureActive() const;
        void ProcessEvents();
        void Shutdown();
        void OnExit(std::move_only_function<void()>&& callback);

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                              RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass,
                                             DWORD_PTR dwRefData);

        void InstallWndProcHook();
        bool InstallWndProcHookAttempt();
        void UninstallWndProcHook() const;

        std::string GetClipboardText() const;
        void SetClipboardText(const std::string& text) const;

        void QueueInputEvent(Cef::CefInputEvent event);
        uint32_t GetMouseModifiers() const;
        void QueueCommittedCharEvent(const std::wstring& utf16Text, LPARAM lParam);

        void HandKeysToView();
        void HandKeysToGame();
        void ResetKeyHandoff();
        bool IsKeyMessageHeldFromGame(UINT uMsg, LPARAM lParam);
        void DropKeysHeldFromView(RE::InputEvent* const* a_event);

        HWND _hWnd{};
        ResourceLock<std::map<Core::PrismaViewId, std::shared_ptr<Core::PrismaView>>>* _viewsMap{};
        ImeHelper _imeHelper;
        Core::PrismaViewId _currentlyFocusedViewId{};
        std::mutex _focusedViewIdMutex;
        std::mutex _eventQueueMutex;
        std::vector<Cef::CefInputEvent> _eventQueue;
        std::atomic<bool> _isAnyInputCaptureActive = false;
        std::atomic<bool> _isFocusedTextInputActive = false;
        // A physical key press reaches the view through WM_KEY* and the game through DirectInput; the two arrive
        // in no fixed order, often a frame apart. A key still held when the keyboard changes hands therefore stays
        // with its previous owner until released, or the press that focuses a view would land in it too, and the
        // press that closes it would reach the game again. Indexed by DirectInput key code.
        struct KeyHandoff {
            std::bitset<256> heldFromGame;  // down for the game when focus started; their messages skip the view
            std::bitset<256> downInView;    // pressed in the view and not released yet
            std::bitset<256> heldFromView;  // still down when focus ended; their game events are dropped
        };
        ResourceLock<KeyHandoff> _keyHandoff;
        // What the game keeps seeing in RE::MenuCursor while a view is focused: the position the cursor had
        // when focus started. Vanilla menus underneath hit-test MenuCursor every frame, so freezing it is
        // what keeps them inert; an off-screen position would make MapMenu edge-scroll to the corner.
        float _freezedCursorX = 0.0f;
        float _freezedCursorY = 0.0f;
        // The real cursor position PrismaUI tracks for CEF and for the drawn arrow. See ProcessEvent.
        float _cursorX = 0.0f;
        float _cursorY = 0.0f;
        std::move_only_function<void()> _onExitCallback{};
        bool _mouseButtonStates[3] = {false, false, false};
        wchar_t _pendingHighSurrogate = 0;
        bool _isInitialized = false;
    };
}
