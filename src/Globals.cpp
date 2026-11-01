#include "Globals.h"

#include <SkyrimSchedulers.h>

ThreadDispatcher& MainThreadDispatcher() { return SkyrimSchedulers::Install().MainThread; }