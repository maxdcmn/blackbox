#pragma once

#include "vram_types.h"
#include <string>

std::string createDetailedResponse(const DetailedVRAMInfo& info);
std::string createAggregatedResponse(const AggregatedVRAMInfo& info);

