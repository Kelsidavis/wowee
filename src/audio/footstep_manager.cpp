#include "audio/footstep_manager.hpp"
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <string>

namespace wowee {
namespace audio {

namespace {

std::vector<std::string> buildClassicFootstepSet(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'L'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootMediumLarge" + material + std::string(1, c) + ".wav");
    }
    return out;
}

std::vector<std::string> buildAltFootstepSet(const std::string& folder, const std::string& stem) {
    std::vector<std::string> out;
    for (int i = 1; i <= 8; ++i) {
        char index[4];
        std::snprintf(index, sizeof(index), "%02d", i);
        out.push_back("Sound\\Character\\Footsteps\\" + folder + "\\" + stem + "_" + index + ".wav");
    }
    return out;
}

} // namespace

FootstepManager::FootstepManager() : rng(std::random_device{}()) {}

FootstepManager::~FootstepManager() {
    shutdown();
}

bool FootstepManager::initialize(pipeline::AssetManager* assets) {
    assetManager = assets;
    sampleCount = 0;
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }

    if (!assetManager) {
        return false;
    }

    preloadSurface(FootstepSurface::STONE, buildClassicFootstepSet("Stone"));
    preloadSurface(FootstepSurface::DIRT, buildClassicFootstepSet("Dirt"));
    preloadSurface(FootstepSurface::GRASS, buildClassicFootstepSet("Grass"));
    preloadSurface(FootstepSurface::WOOD, buildClassicFootstepSet("Wood"));
    preloadSurface(FootstepSurface::SNOW, buildClassicFootstepSet("Snow"));
    preloadSurface(FootstepSurface::WATER, buildClassicFootstepSet("Water"));

    // Alternate naming seen in some builds (especially metals).
    preloadSurface(FootstepSurface::METAL, buildAltFootstepSet("MediumLargeMetalFootsteps", "MediumLargeFootstepMetal"));
    if (surfaces[static_cast<size_t>(FootstepSurface::METAL)].clips.empty()) {
        preloadSurface(FootstepSurface::METAL, buildClassicFootstepSet("Metal"));
    }

    LOG_INFO("Footstep manager initialized (", sampleCount, " clips)");
    return sampleCount > 0;
}

void FootstepManager::shutdown() {
    for (auto& surface : surfaces) {
        surface.clips.clear();
    }
    sampleCount = 0;
    assetManager = nullptr;
}

void FootstepManager::update(float) {
    // No longer needed - AudioEngine handles cleanup internally
}

void FootstepManager::playFootstep(FootstepSurface surface, bool sprinting) {
    if (!assetManager || sampleCount == 0) {
        return;
    }

    // Check if AudioEngine is initialized
    if (!AudioEngine::instance().isInitialized()) {
        return;
    }

    playRandomStep(surface, sprinting);
}

void FootstepManager::preloadSurface(FootstepSurface surface, const std::vector<std::string>& candidates) {
    if (!assetManager) {
        return;
    }

    auto& list = surfaces[static_cast<size_t>(surface)].clips;
    for (const std::string& path : candidates) {
        if (!assetManager->fileExists(path)) {
            continue;
        }
        auto data = assetManager->readFile(path);
        if (data.empty()) {
            continue;
        }
        list.push_back({path, std::move(data)});
        sampleCount++;
    }

    if (!list.empty()) {
        LOG_INFO("Footsteps ", surfaceName(surface), ": loaded ", list.size(), " clips");
    }
}

bool FootstepManager::playRandomStep(FootstepSurface surface, bool sprinting) {
    auto now = std::chrono::steady_clock::now();
    if (lastPlayTime.time_since_epoch().count() != 0) {
        float elapsed = std::chrono::duration<float>(now - lastPlayTime).count();
        float minInterval = sprinting ? 0.09f : 0.14f;
        if (elapsed < minInterval) {
            return false;
        }
    }

    auto& list = surfaces[static_cast<size_t>(surface)].clips;
    if (list.empty()) {
        list = surfaces[static_cast<size_t>(FootstepSurface::STONE)].clips;
        if (list.empty()) {
            return false;
        }
    }

    // Pick a random clip
    std::uniform_int_distribution<size_t> clipDist(0, list.size() - 1);
    const Sample& sample = list[clipDist(rng)];

    // Subtle variation for less repetitive cadence
    std::uniform_real_distribution<float> pitchDist(0.97f, 1.05f);
    std::uniform_real_distribution<float> volumeDist(0.92f, 1.00f);
    float pitch = pitchDist(rng);
    float volume = volumeDist(rng) * (sprinting ? 1.0f : 0.88f) * volumeScale;
    if (volume > 1.0f) volume = 1.0f;
    if (volume < 0.1f) volume = 0.1f;

    // Play using AudioEngine (non-blocking, no process spawn!)
    bool success = AudioEngine::instance().playSound2D(sample.data, volume, pitch);

    if (success) {
        lastPlayTime = now;
        return true;
    }

    return false;
}

const char* FootstepManager::surfaceName(FootstepSurface surface) {
    switch (surface) {
        case FootstepSurface::STONE: return "stone";
        case FootstepSurface::DIRT: return "dirt";
        case FootstepSurface::GRASS: return "grass";
        case FootstepSurface::WOOD: return "wood";
        case FootstepSurface::METAL: return "metal";
        case FootstepSurface::WATER: return "water";
        case FootstepSurface::SNOW: return "snow";
        default: return "unknown";
    }
}

} // namespace audio
} // namespace wowee
