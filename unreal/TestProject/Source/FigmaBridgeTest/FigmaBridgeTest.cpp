// Copyright Rocket Science.

#include "FigmaBridgeTest.h"
#include "Modules/ModuleManager.h"

// The host project has no gameplay code of its own. Everything under test lives
// in the FigmaTokenBridge plugin; this module exists only so UBT has a primary
// game module to build, which is what makes the plugin's C++ compile at all.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, FigmaBridgeTest, "FigmaBridgeTest");
