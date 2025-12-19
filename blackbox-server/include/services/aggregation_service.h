#pragma once

#include "vram_types.h"
#include <vector>
#include <chrono>

AggregatedVRAMInfo collectAggregatedMetrics(unsigned int window_seconds);

