#include "audio/mount_sound_manager.hpp"
#include "audio/audio_engine.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <random>

namespace wowee {
namespace audio {

MountSoundManager::MountSoundManager() {
    lastSoundUpdate_ = std::chrono::steady_clock::now();
}

MountSoundManager::~MountSoundManager() {
    shutdown();
}

bool MountSoundManager::initialize(pipeline::AssetManager* assets) {
    assetManager_ = assets;
    if (!assetManager_) {
        LOG_WARNING("Mount sound manager: no asset manager");
        return false;
    }

    loadMountSounds();

    int totalSamples = wingFlapSounds_.size() + wingIdleSounds_.size() +
                       horseBreathSounds_.size() + horseMoveSounds_.size() +
                       horseJumpSounds_.size() + horseLandSounds_.size();
    LOG_INFO("Mount sound manager initialized (", totalSamples, " clips)");
    return true;
}

void MountSoundManager::shutdown() {
    stopAllMountSounds();
    mounted_ = false;
    wingFlapSounds_.clear();
    wingIdleSounds_.clear();
    horseBreathSounds_.clear();
    horseMoveSounds_.clear();
    assetManager_ = nullptr;
}

void MountSoundManager::loadMountSounds() {
    if (!assetManager_) return;

    // Flying mount wing flaps (movement)
    std::vector<std::string> wingFlapPaths = {
        "Sound\\Creature\\Dragons\\HugeWingFlap1.wav",
        "Sound\\Creature\\Dragons\\HugeWingFlap2.wav",
        "Sound\\Creature\\Dragons\\HugeWingFlap3.wav",
        "Sound\\Creature\\DragonWhelp\\mDragonWhelpWingFlapA.wav",
        "Sound\\Creature\\DragonWhelp\\mDragonWhelpWingFlapB.wav",
    };

    for (const auto& path : wingFlapPaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            wingFlapSounds_.push_back(std::move(sample));
        }
    }

    // Flying mount idle/hovering (screeches/calls)
    std::vector<std::string> wingIdlePaths = {
        "Sound\\Creature\\DragonHawk\\DragonHawkPreAggro.wav",
        "Sound\\Creature\\DragonHawk\\DragonHawkAggro.wav",
        "Sound\\Creature\\Dragons\\DragonPreAggro.wav",
    };

    for (const auto& path : wingIdlePaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            wingIdleSounds_.push_back(std::move(sample));
        }
    }

    // Ground mount breathing/idle (per creature family)
    std::vector<std::string> horseBreathPaths = {
        "Sound\\Creature\\Horse\\mHorseStand3A.wav",
        "Sound\\Creature\\Ram\\RamPreAggro.wav",
        "Sound\\Creature\\Wolf\\mWolfFidget2a.wav",
        "Sound\\Creature\\Tiger\\mTigerStand2A.wav",
    };

    for (const auto& path : horseBreathPaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            horseBreathSounds_.push_back(std::move(sample));
        }
    }

    // Ground mount movement ambient (alerts/whinnies)
    std::vector<std::string> horseMovePaths = {
        "Sound\\Creature\\Horse\\mHorseAggroA.wav",
    };

    for (const auto& path : horseMovePaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            horseMoveSounds_.push_back(std::move(sample));
        }
    }

    // Ground mount jump effort sounds
    std::vector<std::string> horseJumpPaths = {
        "Sound\\Creature\\Horse\\mHorseAttackA.wav",
        "Sound\\Creature\\Horse\\mHorseAttackB.wav",
        "Sound\\Creature\\Horse\\mHorseAttackC.wav",
    };

    for (const auto& path : horseJumpPaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            horseJumpSounds_.push_back(std::move(sample));
        }
    }

    // Ground mount landing thud sounds
    std::vector<std::string> horseLandPaths = {
        "Sound\\Creature\\Horse\\mHorseWoundA.wav",
        "Sound\\Creature\\Horse\\mHorseWoundB.wav",
    };

    for (const auto& path : horseLandPaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            horseLandSounds_.push_back(std::move(sample));
        }
    }

    // Ground mount idle ambient (snorts and whinnies only)
    std::vector<std::string> horseIdlePaths = {
        "Sound\\Creature\\Horse\\mHorseStand3A.wav",  // Snort
        "Sound\\Creature\\Horse\\mHorseAggroA.wav",   // Whinny
    };

    for (const auto& path : horseIdlePaths) {
        MountSample sample;
        if (loadSound(path, sample)) {
            horseIdleSounds_.push_back(std::move(sample));
        }
    }

    if (!wingFlapSounds_.empty()) {
        LOG_INFO("Loaded ", wingFlapSounds_.size(), " wing flap sounds");
    }
    if (!wingIdleSounds_.empty()) {
        LOG_INFO("Loaded ", wingIdleSounds_.size(), " wing idle sounds");
    }
    if (!horseBreathSounds_.empty()) {
        LOG_INFO("Loaded ", horseBreathSounds_.size(), " horse breath sounds");
    }
    if (!horseMoveSounds_.empty()) {
        LOG_INFO("Loaded ", horseMoveSounds_.size(), " horse move sounds");
    }
    if (!horseJumpSounds_.empty()) {
        LOG_INFO("Loaded ", horseJumpSounds_.size(), " horse jump sounds");
    }
    if (!horseLandSounds_.empty()) {
        LOG_INFO("Loaded ", horseLandSounds_.size(), " horse land sounds");
    }
    if (!horseIdleSounds_.empty()) {
        LOG_INFO("Loaded ", horseIdleSounds_.size(), " horse idle sounds (snorts/whinnies)");
    }
}

bool MountSoundManager::loadSound(const std::string& path, MountSample& sample) {
    if (!assetManager_ || !assetManager_->fileExists(path)) {
        LOG_WARNING("Mount sound file not found: ", path);
        return false;
    }

    auto data = assetManager_->readFile(path);
    if (data.empty()) {
        LOG_WARNING("Mount sound file empty: ", path);
        return false;
    }

    sample.path = path;
    sample.data = std::move(data);
    LOG_INFO("Loaded mount sound: ", path);
    return true;
}

void MountSoundManager::update(float deltaTime) {
    if (!mounted_) {
        soundLoopTimer_ = 0.0f;
        return;
    }

    soundLoopTimer_ += deltaTime;
    updateMountSounds();
}

void MountSoundManager::onMount(uint32_t creatureDisplayId, bool isFlying) {
    mounted_ = true;
    currentDisplayId_ = creatureDisplayId;
    currentMountType_ = detectMountType(creatureDisplayId);
    currentMountFamily_ = detectMountFamily(creatureDisplayId);
    flying_ = isFlying;
    moving_ = false;

    LOG_INFO("Mount sound: mounted on display ID ", creatureDisplayId,
             " type=", static_cast<int>(currentMountType_),
             " family=", static_cast<int>(currentMountFamily_),
             " flying=", flying_);

    updateMountSounds();
}

void MountSoundManager::onDismount() {
    stopAllMountSounds();
    mounted_ = false;
    currentMountType_ = MountType::NONE;
    currentDisplayId_ = 0;
    flying_ = false;
    moving_ = false;
}

void MountSoundManager::setMoving(bool moving) {
    if (moving_ != moving) {
        moving_ = moving;
        if (mounted_) {
            updateMountSounds();
        }
    }
}

void MountSoundManager::setFlying(bool flying) {
    if (flying_ != flying) {
        flying_ = flying;
        if (mounted_) {
            updateMountSounds();
        }
    }
}

void MountSoundManager::setGrounded(bool grounded) {
    setFlying(!grounded);
}

void MountSoundManager::playRearUpSound() {
    if (!mounted_) return;

    // Cooldown to prevent spam (200ms)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActionSoundTime_).count();
    if (elapsed < 200) return;
    lastActionSoundTime_ = now;

    // Use semantic sound based on mount family
    if (currentMountType_ == MountType::GROUND && !horseMoveSounds_.empty()) {
        // Ground mounts: whinny/roar
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, horseMoveSounds_.size() - 1);
        const auto& sample = horseMoveSounds_[dist(rng)];
        if (!sample.data.empty()) {
            AudioEngine::instance().playSound2D(sample.data, 0.7f * volumeScale_, 1.0f);
        }
    } else if (currentMountType_ == MountType::FLYING && !wingIdleSounds_.empty()) {
        // Flying mounts: screech/roar
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, wingIdleSounds_.size() - 1);
        const auto& sample = wingIdleSounds_[dist(rng)];
        if (!sample.data.empty()) {
            AudioEngine::instance().playSound2D(sample.data, 0.6f * volumeScale_, 1.1f);
        }
    }
}

void MountSoundManager::playJumpSound() {
    if (!mounted_) return;

    // Cooldown to prevent spam
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActionSoundTime_).count();
    if (elapsed < 200) return;
    lastActionSoundTime_ = now;

    // Jump effort sound - DISABLED (attack sounds have pained growls)
    // if (currentMountType_ == MountType::GROUND && !horseJumpSounds_.empty()) {
    //     static std::mt19937 rng(std::random_device{}());
    //     std::uniform_int_distribution<size_t> dist(0, horseJumpSounds_.size() - 1);
    //     const auto& sample = horseJumpSounds_[dist(rng)];
    //     if (!sample.data.empty()) {
    //         AudioEngine::instance().playSound2D(sample.data, 0.5f * volumeScale_, 1.1f);
    //     }
    // } else
    if (currentMountType_ == MountType::FLYING && !wingFlapSounds_.empty()) {
        // Flying mounts: wing whoosh
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, wingFlapSounds_.size() - 1);
        const auto& sample = wingFlapSounds_[dist(rng)];
        if (!sample.data.empty()) {
            AudioEngine::instance().playSound2D(sample.data, 0.4f * volumeScale_, 1.0f);
        }
    }
}

void MountSoundManager::playLandSound() {
    if (!mounted_) return;

    // Cooldown to prevent spam
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActionSoundTime_).count();
    if (elapsed < 200) return;
    lastActionSoundTime_ = now;

    // Landing thud/hoof sound - DISABLED (wound sounds have pained growls)
    // if (currentMountType_ == MountType::GROUND && !horseLandSounds_.empty()) {
    //     static std::mt19937 rng(std::random_device{}());
    //     std::uniform_int_distribution<size_t> dist(0, horseLandSounds_.size() - 1);
    //     const auto& sample = horseLandSounds_[dist(rng)];
    //     if (!sample.data.empty()) {
    //         AudioEngine::instance().playSound2D(sample.data, 0.6f * volumeScale_, 0.85f);
    //     }
    // }
}

void MountSoundManager::playIdleSound() {
    if (!mounted_ || moving_) return;

    // Ambient idle sounds (snorts and whinnies only for ground mounts)
    if (currentMountType_ == MountType::GROUND && !horseIdleSounds_.empty()) {
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, horseIdleSounds_.size() - 1);
        const auto& sample = horseIdleSounds_[dist(rng)];
        if (!sample.data.empty()) {
            AudioEngine::instance().playSound2D(sample.data, 0.35f * volumeScale_, 0.95f);
        }
    }
    // No idle sounds for flying mounts (wing sounds were too aggressive)
}

MountType MountSoundManager::detectMountType(uint32_t creatureDisplayId) const {
    // TODO: Load from CreatureDisplayInfo.dbc or CreatureModelData.dbc
    // For now, use simple heuristics based on common display IDs
    // This is a placeholder - we'd need proper DBC parsing for accuracy

    // Common flying mount display IDs (approximate ranges)
    // Gryphons: ~2300-2310
    // Wyverns: ~1566-1570
    // Drakes: ~25830-25870
    // Phoenixes: ~17890-17900

    if (creatureDisplayId >= 2300 && creatureDisplayId <= 2320) return MountType::FLYING; // Gryphons
    if (creatureDisplayId >= 1560 && creatureDisplayId <= 1580) return MountType::FLYING; // Wyverns
    if (creatureDisplayId >= 25800 && creatureDisplayId <= 25900) return MountType::FLYING; // Drakes
    if (creatureDisplayId >= 17880 && creatureDisplayId <= 17910) return MountType::FLYING; // Phoenixes

    // Most other mounts are ground
    return MountType::GROUND;
}

MountFamily MountSoundManager::detectMountFamily(uint32_t creatureDisplayId) const {
    // Heuristic creature family detection based on common display ID ranges
    // TODO: Replace with proper CreatureModelData.dbc lookup

    // Horses: ~14000-14999 range (includes many horse variants)
    if (creatureDisplayId >= 14000 && creatureDisplayId < 15000) return MountFamily::HORSE;

    // Rams: ~14349-14375 range
    if (creatureDisplayId >= 14349 && creatureDisplayId <= 14375) return MountFamily::RAM;

    // Wolves: ~207-217, ~2326-2329 ranges
    if ((creatureDisplayId >= 207 && creatureDisplayId <= 217) ||
        (creatureDisplayId >= 2326 && creatureDisplayId <= 2329)) return MountFamily::WOLF;

    // Tigers/Cats: ~6442-6473 range
    if (creatureDisplayId >= 6442 && creatureDisplayId <= 6473) return MountFamily::TIGER;

    // Raptors: ~6466-6474 range
    if (creatureDisplayId >= 6466 && creatureDisplayId <= 6474) return MountFamily::RAPTOR;

    // Dragons/Drakes
    if (creatureDisplayId >= 25800 && creatureDisplayId <= 25900) return MountFamily::DRAGON;

    // Default to horse for unknown ground mounts
    return MountFamily::HORSE;
}

void MountSoundManager::updateMountSounds() {
    if (!AudioEngine::instance().isInitialized() || !mounted_) {
        return;
    }

    static std::mt19937 rng(std::random_device{}());

    // Flying mounts
    if (currentMountType_ == MountType::FLYING && flying_) {
        if (moving_ && !wingFlapSounds_.empty()) {
            // Wing flaps when moving (play periodically for continuous flapping sound)
            if (soundLoopTimer_ >= 1.2f) {
                std::uniform_int_distribution<size_t> dist(0, wingFlapSounds_.size() - 1);
                const auto& sample = wingFlapSounds_[dist(rng)];
                std::uniform_real_distribution<float> volumeDist(0.4f, 0.5f);
                std::uniform_real_distribution<float> pitchDist(0.95f, 1.05f);
                AudioEngine::instance().playSound2D(
                    sample.data,
                    volumeDist(rng) * volumeScale_,
                    pitchDist(rng)
                );
                soundLoopTimer_ = 0.0f;
                playingMovementSound_ = true;
            }
        } else if (!moving_ && !wingIdleSounds_.empty()) {
            // Idle/hovering sounds (less frequent)
            if (soundLoopTimer_ >= 3.5f) {
                std::uniform_int_distribution<size_t> dist(0, wingIdleSounds_.size() - 1);
                const auto& sample = wingIdleSounds_[dist(rng)];
                std::uniform_real_distribution<float> volumeDist(0.3f, 0.4f);
                std::uniform_real_distribution<float> pitchDist(0.98f, 1.02f);
                AudioEngine::instance().playSound2D(
                    sample.data,
                    volumeDist(rng) * volumeScale_,
                    pitchDist(rng)
                );
                soundLoopTimer_ = 0.0f;
                playingIdleSound_ = true;
            }
        }
    }
    // Ground mounts
    else if (currentMountType_ == MountType::GROUND && !flying_) {
        if (moving_ && !horseMoveSounds_.empty()) {
            // Occasional whinny/ambient sounds while moving
            if (soundLoopTimer_ >= 8.0f) {
                std::uniform_int_distribution<size_t> dist(0, horseMoveSounds_.size() - 1);
                const auto& sample = horseMoveSounds_[dist(rng)];
                std::uniform_real_distribution<float> volumeDist(0.35f, 0.45f);
                std::uniform_real_distribution<float> pitchDist(0.97f, 1.03f);
                AudioEngine::instance().playSound2D(
                    sample.data,
                    volumeDist(rng) * volumeScale_,
                    pitchDist(rng)
                );
                soundLoopTimer_ = 0.0f;
                playingMovementSound_ = true;
            }
        } else if (!moving_ && !horseBreathSounds_.empty()) {
            // Breathing/snorting when idle
            if (soundLoopTimer_ >= 4.5f) {
                std::uniform_int_distribution<size_t> dist(0, horseBreathSounds_.size() - 1);
                const auto& sample = horseBreathSounds_[dist(rng)];
                std::uniform_real_distribution<float> volumeDist(0.25f, 0.35f);
                std::uniform_real_distribution<float> pitchDist(0.98f, 1.02f);
                AudioEngine::instance().playSound2D(
                    sample.data,
                    volumeDist(rng) * volumeScale_,
                    pitchDist(rng)
                );
                soundLoopTimer_ = 0.0f;
                playingIdleSound_ = true;
            }
        }
    }
}

void MountSoundManager::stopAllMountSounds() {
    // Reset state flags
    playingMovementSound_ = false;
    playingIdleSound_ = false;
    soundLoopTimer_ = 0.0f;
}

} // namespace audio
} // namespace wowee
