#ifdef __linux__

#include <cstddef>
#include <cstring>
#include <string>
#include <pthread.h>

#include "Stubs/LinuxStubs.h"
#include "../Common/Consoles_App.h"

void Display::update() {
    // No-op on Linux, as we don't have a real display class. This is just to
    // satisfy the linker.
}

int CMinecraftApp::GetTPConfigVal(wchar_t* pwchDataFile) { return 0; }

#include "../../../Minecraft.World/Platform/x64headers/extraX64.h"

void PIXSetMarkerDeprecated(int a, const char* b, ...) {}

#endif
