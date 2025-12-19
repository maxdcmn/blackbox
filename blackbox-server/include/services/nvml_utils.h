#pragma once

#include "vram_types.h"

bool initNVML();
void shutdownNVML();
DetailedVRAMInfo getDetailedVRAMUsage();

