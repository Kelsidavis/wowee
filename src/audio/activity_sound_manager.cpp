#include "audio/activity_sound_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include "platform/process.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <cctype>

namespace wowee {
namespace audio {

namespace {

std::vector<std::string> buildClassicSet(const std::string& material) {
    std::vector<std::string> out;
    for (char c = 'A'; c <= 'L'; ++c) {
        out.push_back("Sound\\Character\\Footsteps\\mFootMediumLarge" + material + std::string(1, c) + ".wav");
    }
    return out;
}

} // namespace

ActivitySoundManager::ActivitySoundManager() : rng(std::random_device{}()) {}
ActivitySoundManager::~ActivitySoundManager() { shutdown(); }

bool ActivitySoundManager::initialize(pipeline::AssetManager* assets) {
    shutdown();
    assetManager = assets;
    if (!assetManager) return false;

    rebuildJumpClipsForProfile("Human", "Human", true);
    rebuildSwimLoopClipsForProfile("Human", "Human", true);
    rebuildHardLandClipsForProfile("Human", "Human", true);

    preloadCandidates(splashEnterClips, {
        "Sound\\Character\\General\\Water\\WaterSplashSmall.wav",
        "Sound\\Character\\General\\Water\\WaterSplashMedium.wav",
        "Sound\\Character\\General\\Water\\WaterSplashLarge.wav",
        "Sound\\Character\\Footsteps\\mFootMediumLargeWaterA.wav",
        "Sound\\Character\\Footsteps\\mFootMediumLargeWaterB.wav",
        "Sound\\Character\\Footsteps\\mFootMediumLargeWaterC.wav",
        "Sound\\Character\\Footsteps\\mFootMediumLargeWaterD.wav"
    });
    splashExitClips = splashEnterClips;

    preloadLandingSet(FootstepSurface::STONE, "Stone");
    preloadLandingSet(FootstepSurface::DIRT, "Dirt");
    preloadLandingSet(FootstepSurface::GRASS, "Grass");
    preloadLandingSet(FootstepSurface::WOOD, "Wood");
    preloadLandingSet(FootstepSurface::METAL, "Metal");
    preloadLandingSet(FootstepSurface::WATER, "Water");
    preloadLandingSet(FootstepSurface::SNOW, "Snow");

    preloadCandidates(meleeSwingClips, {
        "Sound\\Item\\Weapons\\Sword\\SwordSwing1.wav",
        "Sound\\Item\\Weapons\\Sword\\SwordSwing2.wav",
        "Sound\\Item\\Weapons\\Sword\\SwordSwing3.wav",
        "Sound\\Item\\Weapons\\Sword\\SwordHit1.wav",
        "Sound\\Item\\Weapons\\Sword\\SwordHit2.wav",
        "Sound\\Item\\Weapons\\Sword\\SwordHit3.wav",
        "Sound\\Item\\Weapons\\OneHanded\\Sword\\SwordSwing1.wav",
        "Sound\\Item\\Weapons\\OneHanded\\Sword\\SwordSwing2.wav",
        "Sound\\Item\\Weapons\\OneHanded\\Sword\\SwordSwing3.wav",
        "Sound\\Item\\Weapons\\Melee\\MeleeSwing1.wav",
        "Sound\\Item\\Weapons\\Melee\\MeleeSwing2.wav",
        "Sound\\Item\\Weapons\\Melee\\MeleeSwing3.wav"
    });

    initialized = true;
    core::Logger::getInstance().info("Activity SFX loaded: jump=", jumpClips.size(),
                                     " splash=", splashEnterClips.size(),
                                     " swimLoop=", swimLoopClips.size());
    return true;
}

void ActivitySoundManager::shutdown() {
    stopSwimLoop();
    stopOneShot();
    std::remove(loopTempPath.c_str());
    std::remove(oneShotTempPath.c_str());
    for (auto& set : landingSets) set.clips.clear();
    jumpClips.clear();
    splashEnterClips.clear();
    splashExitClips.clear();
    swimLoopClips.clear();
    hardLandClips.clear();
    meleeSwingClips.clear();
    swimmingActive = false;
    swimMoving = false;
    initialized = false;
    assetManager = nullptr;
}

void ActivitySoundManager::update(float) {
    reapProcesses();
}

void ActivitySoundManager::preloadCandidates(std::vector<Sample>& out, const std::vector<std::string>& candidates) {
    if (!assetManager) return;
    for (const auto& path : candidates) {
        if (!assetManager->fileExists(path)) continue;
        auto data = assetManager->readFile(path);
        if (data.empty()) continue;
        out.push_back({path, std::move(data)});
    }
}

void ActivitySoundManager::preloadLandingSet(FootstepSurface surface, const std::string& material) {
    auto& clips = landingSets[static_cast<size_t>(surface)].clips;
    preloadCandidates(clips, buildClassicSet(material));
}

void ActivitySoundManager::rebuildJumpClipsForProfile(const std::string& raceFolder, const std::string& raceBase, bool male) {
    jumpClips.clear();
    const std::string gender = male ? "Male" : "Female";
    const std::string prefix = "Sound\\Character\\" + raceFolder + "\\";
    const std::string stem = raceBase + gender;
    const std::string genderDir = male ? "Male" : "Female";
    preloadCandidates(jumpClips, {
        // Common WotLK-style variants.
        prefix + stem + "\\" + stem + "Jump01.wav",
        prefix + stem + "\\" + stem + "Jump02.wav",
        prefix + stem + "\\" + stem + "Jump03.wav",
        prefix + stem + "\\" + stem + "Exertion01.wav",
        prefix + stem + "\\" + stem + "Exertion02.wav",
        prefix + stem + "JumpA.wav",
        prefix + stem + "JumpB.wav",
        prefix + stem + "JumpC.wav",
        prefix + stem + "Jump.wav",
        prefix + stem + "JumpStart.wav",
        prefix + stem + "Land.wav",
        prefix + genderDir + "\\" + stem + "JumpA.wav",
        prefix + genderDir + "\\" + stem + "JumpB.wav",
        prefix + genderDir + "\\" + stem + "JumpC.wav",
        prefix + genderDir + "\\" + stem + "Jump.wav",
        prefix + genderDir + "\\" + stem + "JumpStart.wav",
        prefix + raceBase + "JumpA.wav",
        prefix + raceBase + "JumpB.wav",
        prefix + raceBase + "JumpC.wav",
        prefix + raceBase + "Jump.wav",
        prefix + raceBase + "\\" + stem + "JumpA.wav",
        prefix + raceBase + "\\" + stem + "JumpB.wav",
        prefix + raceBase + "\\" + stem + "JumpC.wav",
        // Alternate folder naming in some packs.
        "Sound\\Character\\" + stem + "\\" + stem + "JumpA.wav",
        "Sound\\Character\\" + stem + "\\" + stem + "JumpB.wav",
        "Sound\\Character\\" + stem + "\\" + stem + "Jump.wav",
        // Fallback safety
        "Sound\\Character\\Human\\HumanMaleJumpA.wav",
        "Sound\\Character\\Human\\HumanMaleJumpB.wav",
        "Sound\\Character\\Human\\HumanFemaleJumpA.wav",
        "Sound\\Character\\Human\\HumanFemaleJumpB.wav",
        "Sound\\Character\\Human\\Male\\HumanMaleJumpA.wav",
        "Sound\\Character\\Human\\Male\\HumanMaleJumpB.wav",
        "Sound\\Character\\Human\\Female\\HumanFemaleJumpA.wav",
        "Sound\\Character\\Human\\Female\\HumanFemaleJumpB.wav",
        "Sound\\Character\\Human\\HumanMale\\HumanMaleJump01.wav",
        "Sound\\Character\\Human\\HumanMale\\HumanMaleJump02.wav",
        "Sound\\Character\\Human\\HumanMale\\HumanMaleJump03.wav",
        "Sound\\Character\\Human\\HumanFemale\\HumanFemaleJump01.wav",
        "Sound\\Character\\Human\\HumanFemale\\HumanFemaleJump02.wav",
        "Sound\\Character\\Human\\HumanFemale\\HumanFemaleJump03.wav",
        "Sound\\Character\\HumanMale\\HumanMaleJumpA.wav",
        "Sound\\Character\\HumanMale\\HumanMaleJumpB.wav",
        "Sound\\Character\\HumanFemale\\HumanFemaleJumpA.wav",
        "Sound\\Character\\HumanFemale\\HumanFemaleJumpB.wav"
    });
}

void ActivitySoundManager::rebuildSwimLoopClipsForProfile(const std::string& raceFolder, const std::string& raceBase, bool male) {
    swimLoopClips.clear();
    const std::string gender = male ? "Male" : "Female";
    const std::string prefix = "Sound\\Character\\" + raceFolder + "\\";
    const std::string stem = raceBase + gender;
    preloadCandidates(swimLoopClips, {
        prefix + stem + "\\" + stem + "SwimLoop.wav",
        prefix + stem + "\\" + stem + "Swim01.wav",
        prefix + stem + "\\" + stem + "Swim02.wav",
        prefix + stem + "SwimLoop.wav",
        prefix + stem + "Swim01.wav",
        prefix + stem + "Swim02.wav",
        prefix + (male ? "Male" : "Female") + "\\" + stem + "SwimLoop.wav",
        "Sound\\Character\\Swim\\SwimMoveLoop.wav",
        "Sound\\Character\\Swim\\SwimLoop.wav",
        "Sound\\Character\\Swim\\SwimSlowLoop.wav"
    });
    if (swimLoopClips.empty()) {
        preloadCandidates(swimLoopClips, buildClassicSet("Water"));
    }
}

void ActivitySoundManager::rebuildHardLandClipsForProfile(const std::string& raceFolder, const std::string& raceBase, bool male) {
    hardLandClips.clear();
    const std::string gender = male ? "Male" : "Female";
    const std::string prefix = "Sound\\Character\\" + raceFolder + "\\";
    const std::string stem = raceBase + gender;
    preloadCandidates(hardLandClips, {
        prefix + stem + "\\" + stem + "LandHard01.wav",
        prefix + stem + "\\" + stem + "LandHard02.wav",
        prefix + stem + "LandHard01.wav",
        prefix + stem + "LandHard02.wav"
    });
}

bool ActivitySoundManager::playOneShot(const std::vector<Sample>& clips, float volume, float pitchLo, float pitchHi) {
    if (clips.empty()) return false;
    reapProcesses();
    if (oneShotPid != INVALID_PROCESS) return false;

    std::uniform_int_distribution<size_t> clipDist(0, clips.size() - 1);
    const Sample& sample = clips[clipDist(rng)];
    std::ofstream out(oneShotTempPath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(sample.data.data()), static_cast<std::streamsize>(sample.data.size()));
    out.close();

    std::uniform_real_distribution<float> pitchDist(pitchLo, pitchHi);
    float pitch = pitchDist(rng);
    volume *= volumeScale;
    if (volume < 0.1f) volume = 0.1f;
    if (volume > 1.2f) volume = 1.2f;
    std::string filter = "asetrate=44100*" + std::to_string(pitch) +
                         ",aresample=44100,volume=" + std::to_string(volume);

    oneShotPid = platform::spawnProcess({
        "-nodisp", "-autoexit", "-loglevel", "quiet",
        "-af", filter, oneShotTempPath
    });

    return oneShotPid != INVALID_PROCESS;
}

void ActivitySoundManager::startSwimLoop() {
    if (swimLoopPid != INVALID_PROCESS || swimLoopClips.empty()) return;
    std::uniform_int_distribution<size_t> clipDist(0, swimLoopClips.size() - 1);
    const Sample& sample = swimLoopClips[clipDist(rng)];

    std::ofstream out(loopTempPath, std::ios::binary);
    if (!out) return;
    out.write(reinterpret_cast<const char*>(sample.data.data()), static_cast<std::streamsize>(sample.data.size()));
    out.close();

    float volume = (swimMoving ? 0.85f : 0.65f) * volumeScale;
    std::string filter = "volume=" + std::to_string(volume);

    swimLoopPid = platform::spawnProcess({
        "-nodisp", "-autoexit", "-loop", "0", "-loglevel", "quiet",
        "-af", filter, loopTempPath
    });
}

void ActivitySoundManager::stopSwimLoop() {
    platform::killProcess(swimLoopPid);
}

void ActivitySoundManager::stopOneShot() {
    platform::killProcess(oneShotPid);
}

void ActivitySoundManager::reapProcesses() {
    if (oneShotPid != INVALID_PROCESS) {
        platform::isProcessRunning(oneShotPid);
    }
    if (swimLoopPid != INVALID_PROCESS) {
        platform::isProcessRunning(swimLoopPid);
    }
}

void ActivitySoundManager::playJump() {
    // DISABLED: Activity sounds spawn processes which causes stuttering
    return;

    auto now = std::chrono::steady_clock::now();
    if (lastJumpAt.time_since_epoch().count() != 0) {
        if (std::chrono::duration<float>(now - lastJumpAt).count() < 0.35f) return;
    }
    if (playOneShot(jumpClips, 0.72f, 0.98f, 1.04f)) {
        lastJumpAt = now;
    }
}

void ActivitySoundManager::playLanding(FootstepSurface surface, bool hardLanding) {
    // DISABLED: Activity sounds spawn processes which causes stuttering
    return;

    auto now = std::chrono::steady_clock::now();
    if (lastLandAt.time_since_epoch().count() != 0) {
        if (std::chrono::duration<float>(now - lastLandAt).count() < 0.10f) return;
    }
    const auto& clips = landingSets[static_cast<size_t>(surface)].clips;
    if (playOneShot(clips, hardLanding ? 1.00f : 0.82f, 0.95f, 1.03f)) {
        lastLandAt = now;
    }
    if (hardLanding) {
        playOneShot(hardLandClips, 0.84f, 0.97f, 1.03f);
    }
}

void ActivitySoundManager::playMeleeSwing() {
    if (meleeSwingClips.empty()) {
        if (!meleeSwingWarned) {
            core::Logger::getInstance().warning("No melee swing SFX found in assets");
            meleeSwingWarned = true;
        }
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (lastMeleeSwingAt.time_since_epoch().count() != 0) {
        if (std::chrono::duration<float>(now - lastMeleeSwingAt).count() < 0.12f) return;
    }
    if (playOneShot(meleeSwingClips, 0.80f, 0.96f, 1.04f)) {
        lastMeleeSwingAt = now;
    }
}

void ActivitySoundManager::setSwimmingState(bool swimming, bool moving) {
    swimMoving = moving;
    if (swimming == swimmingActive) return;
    swimmingActive = swimming;
    if (swimmingActive) {
        startSwimLoop();
    } else {
        stopSwimLoop();
    }
}

void ActivitySoundManager::setCharacterVoiceProfile(const std::string& modelName) {
    if (!assetManager || modelName.empty()) return;

    std::string lower = modelName;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    bool male = (lower.find("female") == std::string::npos);
    std::string folder = "Human";
    std::string base = "Human";

    struct RaceMap { const char* token; const char* folder; const char* base; };
    static const RaceMap races[] = {
        {"human", "Human", "Human"},
        {"orc", "Orc", "Orc"},
        {"dwarf", "Dwarf", "Dwarf"},
        {"nightelf", "NightElf", "NightElf"},
        {"scourge", "Scourge", "Scourge"},
        {"undead", "Scourge", "Scourge"},
        {"tauren", "Tauren", "Tauren"},
        {"gnome", "Gnome", "Gnome"},
        {"troll", "Troll", "Troll"},
        {"bloodelf", "BloodElf", "BloodElf"},
        {"draenei", "Draenei", "Draenei"},
        {"goblin", "Goblin", "Goblin"},
        {"worgen", "Worgen", "Worgen"},
    };
    for (const auto& r : races) {
        if (lower.find(r.token) != std::string::npos) {
            folder = r.folder;
            base = r.base;
            break;
        }
    }

    std::string key = folder + "|" + base + "|" + (male ? "M" : "F");
    if (key == voiceProfileKey) return;
    voiceProfileKey = key;
    rebuildJumpClipsForProfile(folder, base, male);
    rebuildSwimLoopClipsForProfile(folder, base, male);
    rebuildHardLandClipsForProfile(folder, base, male);
    core::Logger::getInstance().info("Activity SFX voice profile: ", voiceProfileKey,
                                     " jump clips=", jumpClips.size(),
                                     " swim clips=", swimLoopClips.size(),
                                     " hardLand clips=", hardLandClips.size());
}

void ActivitySoundManager::playWaterEnter() {
    auto now = std::chrono::steady_clock::now();
    if (lastSplashAt.time_since_epoch().count() != 0) {
        if (std::chrono::duration<float>(now - lastSplashAt).count() < 0.20f) return;
    }
    if (playOneShot(splashEnterClips, 0.95f, 0.95f, 1.05f)) {
        lastSplashAt = now;
    }
}

void ActivitySoundManager::playWaterExit() {
    auto now = std::chrono::steady_clock::now();
    if (lastSplashAt.time_since_epoch().count() != 0) {
        if (std::chrono::duration<float>(now - lastSplashAt).count() < 0.20f) return;
    }
    if (playOneShot(splashExitClips, 0.95f, 0.95f, 1.05f)) {
        lastSplashAt = now;
    }
}

} // namespace audio
} // namespace wowee
