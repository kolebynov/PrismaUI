#pragma once

#include <ThreadDispatcher.h>

// SkyrimSchedulers' main-thread dispatcher; SKSEPlugin_Load creates it on the main thread.
ThreadDispatcher& MainThreadDispatcher();
