#pragma once

// This file is required.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// Since CommonLibSSE-NG 6.x, RE/S/State.h pulls in DirectXTK's <SimpleMath.h>,
// which drags in <windows.h>. Its A/W macros then shadow identically named
// CommonLibSSE members (e.g. GetObject -> GetObjectW on InventoryEntryData).
#undef GetObject
#undef GetClassName
#undef GetMessage

using namespace std::literals;
