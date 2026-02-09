#include "core/memory_monitor.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <string>
#include <sys/sysinfo.h>

namespace wowee {
namespace core {

MemoryMonitor& MemoryMonitor::getInstance() {
    static MemoryMonitor instance;
    return instance;
}

void MemoryMonitor::initialize() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        totalRAM_ = static_cast<size_t>(info.totalram) * info.mem_unit;
        LOG_INFO("System RAM detected: ", totalRAM_ / (1024 * 1024 * 1024), " GB");
    } else {
        // Fallback: assume 16GB
        totalRAM_ = 16ull * 1024 * 1024 * 1024;
        LOG_WARNING("Could not detect system RAM, assuming 16GB");
    }
}

size_t MemoryMonitor::getAvailableRAM() const {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Available = free + buffers + cached
        return static_cast<size_t>(info.freeram) * info.mem_unit;
    }
    return totalRAM_ / 2;  // Fallback: assume 50% available
}

size_t MemoryMonitor::getRecommendedCacheBudget() const {
    size_t available = getAvailableRAM();
    // Use 80% of available RAM for caches (very aggressive), but cap at 90% of total
    size_t budget = available * 80 / 100;
    size_t maxBudget = totalRAM_ * 90 / 100;
    return budget < maxBudget ? budget : maxBudget;
}

bool MemoryMonitor::isMemoryPressure() const {
    size_t available = getAvailableRAM();
    // Memory pressure if < 20% RAM available
    return available < (totalRAM_ * 20 / 100);
}

} // namespace core
} // namespace wowee
