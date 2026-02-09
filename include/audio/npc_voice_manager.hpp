#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <random>
#include <chrono>
#include <glm/glm.hpp>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace audio {

struct VoiceSample {
    std::string path;
    std::vector<uint8_t> data;
};

// NPC voice types (based on creature model/gender)
enum class VoiceType {
    HUMAN_MALE,
    HUMAN_FEMALE,
    DWARF_MALE,
    DWARF_FEMALE,
    NIGHTELF_MALE,
    NIGHTELF_FEMALE,
    ORC_MALE,
    ORC_FEMALE,
    TAUREN_MALE,
    TAUREN_FEMALE,
    TROLL_MALE,
    TROLL_FEMALE,
    UNDEAD_MALE,
    UNDEAD_FEMALE,
    GNOME_MALE,
    GNOME_FEMALE,
    GENERIC,  // Fallback
};

class NpcVoiceManager {
public:
    NpcVoiceManager();
    ~NpcVoiceManager();

    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    // Play greeting sound for NPC at given position
    void playGreeting(uint64_t npcGuid, VoiceType voiceType, const glm::vec3& position);

    void setVolumeScale(float scale) { volumeScale_ = scale; }
    float getVolumeScale() const { return volumeScale_; }

private:
    void loadVoiceSounds();
    bool loadSound(const std::string& path, VoiceSample& sample);
    VoiceType detectVoiceType(uint32_t creatureEntry) const;

    pipeline::AssetManager* assetManager_ = nullptr;
    float volumeScale_ = 1.0f;

    // Voice samples grouped by type
    std::unordered_map<VoiceType, std::vector<VoiceSample>> voiceLibrary_;

    // Cooldown tracking (prevent spam clicking same NPC)
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> lastPlayTime_;
    static constexpr float GREETING_COOLDOWN = 2.0f;  // seconds

    std::mt19937 rng_;
};

} // namespace audio
} // namespace wowee
