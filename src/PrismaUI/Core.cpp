#include "Core.h"

#include <vector>

#include "Cef/Browser/CefRuntime.h"
#include "InputHandler.h"
#include "ViewManager.h"

namespace PrismaUI::Core {
    using namespace PrismaUI::ViewManager;

    std::atomic_uint64_t nextViewId = {1};

    ResourceLock<std::map<PrismaViewId, std::shared_ptr<PrismaView>>> views;

    ResourceLock<std::map<std::pair<PrismaViewId, std::string>, JSCallbackData>> PrismaUI::Core::jsCallbacks;
}  // namespace PrismaUI::Core
