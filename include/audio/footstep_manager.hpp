#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <chrono>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace audio {

enum class FootstepSurface : uint8_t {
    STONE = 0,
    DIRT,
    GRASS,
    WOOD,
    METAL,
    WATER,
    SNOW
};

class FootstepManager {
public:
    FootstepManager();
    ~FootstepManager();

    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    void update(float deltaTime);
    void playFootstep(FootstepSurface surface, bool sprinting);
    void setVolumeScale(float scale) { volumeScale = scale; }
    float getVolumeScale() const { return volumeScale; }

    bool isInitialized() const { return assetManager != nullptr; }
    bool hasAnySamples() const { return sampleCount > 0; }

private:
    struct Sample {
        std::string path;
        std::vector<uint8_t> data;
    };

    struct SurfaceSamples {
        std::vector<Sample> clips;
    };

    void preloadSurface(FootstepSurface surface, const std::vector<std::string>& candidates);
    bool playRandomStep(FootstepSurface surface, bool sprinting);
    static const char* surfaceName(FootstepSurface surface);

    pipeline::AssetManager* assetManager = nullptr;
    SurfaceSamples surfaces[7];
    size_t sampleCount = 0;

    std::chrono::steady_clock::time_point lastPlayTime = std::chrono::steady_clock::time_point{};

    std::mt19937 rng;
    float volumeScale = 1.0f;
};

} // namespace audio
} // namespace wowee
