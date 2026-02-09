#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee {
namespace core {

/**
 * Monitors system memory and provides dynamic cache sizing
 */
class MemoryMonitor {
public:
    static MemoryMonitor& getInstance();

    /**
     * Initialize memory monitoring
     */
    void initialize();

    /**
     * Get total system RAM in bytes
     */
    size_t getTotalRAM() const { return totalRAM_; }

    /**
     * Get currently available RAM in bytes
     */
    size_t getAvailableRAM() const;

    /**
     * Get recommended cache budget (30% of available RAM)
     */
    size_t getRecommendedCacheBudget() const;

    /**
     * Check if system is under memory pressure
     */
    bool isMemoryPressure() const;

private:
    MemoryMonitor() = default;
    size_t totalRAM_ = 0;
};

} // namespace core
} // namespace wowee
