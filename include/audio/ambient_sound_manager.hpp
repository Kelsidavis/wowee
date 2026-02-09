#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <glm/vec3.hpp>

namespace wowee {
namespace pipeline {
class AssetManager;
}

namespace audio {

class AmbientSoundManager {
public:
    AmbientSoundManager() = default;
    ~AmbientSoundManager() = default;

    // Initialization
    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    // Main update loop - called from renderer
    void update(float deltaTime, const glm::vec3& cameraPos, bool isIndoor, bool isSwimming = false, bool isBlacksmith = false);

    // Emitter management
    enum class AmbientType {
        FIREPLACE_SMALL,
        FIREPLACE_LARGE,
        TORCH,
        FOUNTAIN,
        WATER_SURFACE,
        RIVER,
        WATERFALL,
        WIND,
        BIRD_DAY,
        CRICKET_NIGHT,
        OWL_NIGHT
    };

    uint64_t addEmitter(const glm::vec3& position, AmbientType type);
    void removeEmitter(uint64_t id);
    void clearEmitters();

    // Time of day control (0-24 hours)
    void setGameTime(float hours);

    // Volume control
    void setVolumeScale(float scale);

private:
    struct AmbientEmitter {
        uint64_t id;
        AmbientType type;
        glm::vec3 position;
        bool active;
        float lastPlayTime;
        float loopInterval;  // For periodic/looping sounds
    };

    struct AmbientSample {
        std::string path;
        std::vector<uint8_t> data;
        bool loaded;
    };

    // Sound libraries
    std::vector<AmbientSample> fireSoundsSmall_;
    std::vector<AmbientSample> fireSoundsLarge_;
    std::vector<AmbientSample> torchSounds_;
    std::vector<AmbientSample> waterSounds_;
    std::vector<AmbientSample> riverSounds_;
    std::vector<AmbientSample> waterfallSounds_;
    std::vector<AmbientSample> windSounds_;
    std::vector<AmbientSample> tavernSounds_;
    std::vector<AmbientSample> blacksmithSounds_;

    // Active emitters
    std::vector<AmbientEmitter> emitters_;
    uint64_t nextEmitterId_ = 1;

    // State tracking
    float gameTimeHours_ = 12.0f;  // Default noon
    float volumeScale_ = 1.0f;
    float birdTimer_ = 0.0f;
    float cricketTimer_ = 0.0f;
    float windLoopTime_ = 0.0f;
    float blacksmithLoopTime_ = 0.0f;
    bool wasIndoor_ = false;
    bool wasBlacksmith_ = false;
    bool initialized_ = false;

    // Active audio tracking
    struct ActiveSound {
        uint64_t emitterId;
        float startTime;
    };
    std::vector<ActiveSound> activeSounds_;

    // Helper methods
    void updatePositionalEmitters(float deltaTime, const glm::vec3& cameraPos);
    void updatePeriodicSounds(float deltaTime, bool isIndoor, bool isSwimming);
    void updateWindAmbience(float deltaTime, bool isIndoor);
    void updateBlacksmithAmbience(float deltaTime);
    bool loadSound(const std::string& path, AmbientSample& sample, pipeline::AssetManager* assets);

    // Time of day helpers
    bool isDaytime() const { return gameTimeHours_ >= 6.0f && gameTimeHours_ < 20.0f; }
    bool isNighttime() const { return !isDaytime(); }
};

} // namespace audio
} // namespace wowee
