#include "services/aggregation_service.h"
#include "services/nvml_utils.h"
#include "services/vllm_client.h"
#include "services/model_manager.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <thread>
#include <chrono>

static double calculatePercentile(const std::vector<double>& sorted_values, double percentile) {
    if (sorted_values.empty()) return 0.0;
    if (sorted_values.size() == 1) return sorted_values[0];
    
    double index = percentile * (sorted_values.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));
    
    if (lower == upper) {
        return sorted_values[lower];
    }
    
    double weight = index - lower;
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight;
}

static AggregatedStats calculateStats(const std::vector<double>& values) {
    AggregatedStats stats{0.0, 0.0, 0.0, 0.0, 0.0, 0};
    
    if (values.empty()) {
        return stats;
    }
    
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    
    stats.count = static_cast<unsigned int>(values.size());
    stats.min = sorted.front();
    stats.max = sorted.back();
    
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    stats.avg = sum / values.size();
    
    stats.p95 = calculatePercentile(sorted, 0.95);
    stats.p99 = calculatePercentile(sorted, 0.99);
    
    return stats;
}

AggregatedVRAMInfo collectAggregatedMetrics(unsigned int window_seconds) {
    AggregatedVRAMInfo result{};
    result.window_seconds = window_seconds;
    result.total_vram_bytes = 0;
    
    std::vector<double> allocated_vram_samples;
    std::vector<double> used_kv_cache_samples;
    std::vector<double> prefix_hit_rate_samples;
    std::vector<double> requests_running_samples;
    std::vector<double> requests_waiting_samples;
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(window_seconds);
    
    unsigned int sample_count = 0;
    const unsigned int max_samples = 100;
    const auto sample_interval = std::chrono::milliseconds(500); // Sample every 500ms
    
    while (std::chrono::steady_clock::now() < end_time && sample_count < max_samples) {
        DetailedVRAMInfo info = getDetailedVRAMUsage();
        
        if (result.total_vram_bytes == 0) {
            result.total_vram_bytes = info.total;
        }
        
        allocated_vram_samples.push_back(static_cast<double>(info.used));
        used_kv_cache_samples.push_back(static_cast<double>(info.used_kv_cache_bytes));
        prefix_hit_rate_samples.push_back(info.prefix_cache_hit_rate);
        
        auto models_data = fetchPerModelBlockData();
        unsigned int total_requests_running = 0;
        unsigned int total_requests_waiting = 0;
        
        for (const auto& model_data : models_data) {
            if (model_data.available) {
                total_requests_running += model_data.num_requests_running;
                total_requests_waiting += model_data.num_requests_waiting;
            }
        }
        
        requests_running_samples.push_back(static_cast<double>(total_requests_running));
        requests_waiting_samples.push_back(static_cast<double>(total_requests_waiting));
        
        sample_count++;
        
        if (std::chrono::steady_clock::now() < end_time) {
            std::this_thread::sleep_for(sample_interval);
        }
    }
    
    result.sample_count = sample_count;
    result.allocated_vram_bytes = calculateStats(allocated_vram_samples);
    result.used_kv_cache_bytes = calculateStats(used_kv_cache_samples);
    result.prefix_cache_hit_rate = calculateStats(prefix_hit_rate_samples);
    result.num_requests_running = calculateStats(requests_running_samples);
    result.num_requests_waiting = calculateStats(requests_waiting_samples);
    
    // Get final snapshot to extract actual model data (only running models)
    DetailedVRAMInfo final_info = getDetailedVRAMUsage();
    result.models.clear();
    for (const auto& model : final_info.models) {
        // Only include models that have allocated VRAM (running models)
        if (model.allocated_vram_bytes > 0) {
            result.models.push_back(model);
        }
    }
    
    return result;
}

