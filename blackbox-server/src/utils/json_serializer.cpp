#include "utils/json_serializer.h"
#include <sstream>
#include <iomanip>

std::string createDetailedResponse(const DetailedVRAMInfo& info) {
    std::ostringstream oss;
    // Simplified response: total VRAM, allocated VRAM, used KV cache bytes, prefix cache hit rate, and per-model breakdown
    oss << R"({"total_vram_bytes":)" << info.total
        << R"(,"allocated_vram_bytes":)" << info.used
        << R"(,"used_kv_cache_bytes":)" << info.used_kv_cache_bytes
        << R"(,"prefix_cache_hit_rate":)" << std::fixed << std::setprecision(2) << info.prefix_cache_hit_rate
        << R"(,"models":[)";
    
    for (size_t i = 0; i < info.models.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& model = info.models[i];
        oss << R"({"model_id":")" << model.model_id << R"(")"
            << R"(,"port":)" << model.port
            << R"(,"allocated_vram_bytes":)" << model.allocated_vram_bytes
            << R"(,"used_kv_cache_bytes":)" << model.used_kv_cache_bytes
            << "}";
    }
    
    oss << "]}";
    return oss.str();
}

std::string createAggregatedResponse(const AggregatedVRAMInfo& info) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << R"({"total_vram_bytes":)" << info.total_vram_bytes
        << R"(,"window_seconds":)" << info.window_seconds
        << R"(,"sample_count":)" << info.sample_count
        << R"(,"allocated_vram_bytes":{"min":)" << info.allocated_vram_bytes.min
        << R"(,"max":)" << info.allocated_vram_bytes.max
        << R"(,"avg":)" << info.allocated_vram_bytes.avg
        << R"(,"p95":)" << info.allocated_vram_bytes.p95
        << R"(,"p99":)" << info.allocated_vram_bytes.p99
        << R"(,"count":)" << info.allocated_vram_bytes.count << "}"
        << R"(,"used_kv_cache_bytes":{"min":)" << info.used_kv_cache_bytes.min
        << R"(,"max":)" << info.used_kv_cache_bytes.max
        << R"(,"avg":)" << info.used_kv_cache_bytes.avg
        << R"(,"p95":)" << info.used_kv_cache_bytes.p95
        << R"(,"p99":)" << info.used_kv_cache_bytes.p99
        << R"(,"count":)" << info.used_kv_cache_bytes.count << "}"
        << R"(,"prefix_cache_hit_rate":{"min":)" << info.prefix_cache_hit_rate.min
        << R"(,"max":)" << info.prefix_cache_hit_rate.max
        << R"(,"avg":)" << info.prefix_cache_hit_rate.avg
        << R"(,"p95":)" << info.prefix_cache_hit_rate.p95
        << R"(,"p99":)" << info.prefix_cache_hit_rate.p99
        << R"(,"count":)" << info.prefix_cache_hit_rate.count << "}"
        << R"(,"num_requests_running":{"min":)" << info.num_requests_running.min
        << R"(,"max":)" << info.num_requests_running.max
        << R"(,"avg":)" << info.num_requests_running.avg
        << R"(,"p95":)" << info.num_requests_running.p95
        << R"(,"p99":)" << info.num_requests_running.p99
        << R"(,"count":)" << info.num_requests_running.count << "}"
        << R"(,"num_requests_waiting":{"min":)" << info.num_requests_waiting.min
        << R"(,"max":)" << info.num_requests_waiting.max
        << R"(,"avg":)" << info.num_requests_waiting.avg
        << R"(,"p95":)" << info.num_requests_waiting.p95
        << R"(,"p99":)" << info.num_requests_waiting.p99
        << R"(,"count":)" << info.num_requests_waiting.count << "}"
        << R"(,"models":[)";
    
    for (size_t i = 0; i < info.models.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& model = info.models[i];
        oss << R"({"model_id":")" << model.model_id << R"(")"
            << R"(,"port":)" << model.port
            << R"(,"allocated_vram_bytes":)" << model.allocated_vram_bytes
            << R"(,"used_kv_cache_bytes":)" << model.used_kv_cache_bytes
            << "}";
    }
    
    oss << "]}";
    return oss.str();
}

