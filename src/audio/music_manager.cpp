#include "audio/music_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include "platform/process.hpp"
#include <fstream>
#include <filesystem>

namespace wowee {
namespace audio {

MusicManager::MusicManager() {
    tempFilePath = platform::getTempFilePath("wowee_music.mp3");
}

MusicManager::~MusicManager() {
    shutdown();
}

bool MusicManager::initialize(pipeline::AssetManager* assets) {
    assetManager = assets;
    LOG_INFO("Music manager initialized");
    return true;
}

void MusicManager::shutdown() {
    stopCurrentProcess();
    // Clean up temp file
    std::remove(tempFilePath.c_str());
}

void MusicManager::playMusic(const std::string& mpqPath, bool loop) {
    if (!assetManager) return;
    if (mpqPath == currentTrack && playing) return;

    // Read music file from MPQ
    auto data = assetManager->readFile(mpqPath);
    if (data.empty()) {
        LOG_WARNING("Music: Could not read: ", mpqPath);
        return;
    }

    // Stop current playback
    stopCurrentProcess();

    // Write to temp file
    std::ofstream out(tempFilePath, std::ios::binary);
    if (!out) {
        LOG_ERROR("Music: Could not write temp file");
        return;
    }
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();

    // Play with ffplay in background
    std::vector<std::string> args;
    args.push_back("-nodisp");
    args.push_back("-autoexit");
    if (loop) {
        args.push_back("-loop");
        args.push_back("0");
    }
    args.push_back("-volume");
    args.push_back("30");
    args.push_back(tempFilePath);

    playerPid = platform::spawnProcess(args);
    if (playerPid != INVALID_PROCESS) {
        playing = true;
        currentTrack = mpqPath;
        LOG_INFO("Music: Playing ", mpqPath);
    } else {
        LOG_ERROR("Music: Failed to spawn ffplay process");
    }
}

void MusicManager::playFilePath(const std::string& filePath, bool loop) {
    if (filePath.empty()) return;
    if (filePath == currentTrack && playing) return;
    if (!std::filesystem::exists(filePath)) {
        LOG_WARNING("Music: file not found: ", filePath);
        return;
    }

    stopCurrentProcess();

    std::vector<std::string> args;
    args.push_back("-nodisp");
    args.push_back("-autoexit");
    if (loop) {
        args.push_back("-loop");
        args.push_back("0");
    }
    args.push_back("-volume");
    args.push_back("30");
    args.push_back(filePath);

    playerPid = platform::spawnProcess(args);
    if (playerPid != INVALID_PROCESS) {
        playing = true;
        currentTrack = filePath;
        LOG_INFO("Music: Playing file ", filePath);
    } else {
        LOG_ERROR("Music: Failed to spawn ffplay process");
    }
}

void MusicManager::stopMusic(float fadeMs) {
    (void)fadeMs;  // ffplay doesn't support fade easily
    stopCurrentProcess();
    playing = false;
    currentTrack.clear();
}

void MusicManager::crossfadeTo(const std::string& mpqPath, float fadeMs) {
    if (mpqPath == currentTrack && playing) return;

    // Simple implementation: stop and start (no actual crossfade with subprocess)
    if (fadeMs > 0 && playing) {
        crossfading = true;
        pendingTrack = mpqPath;
        fadeTimer = 0.0f;
        fadeDuration = fadeMs / 1000.0f;
        stopCurrentProcess();
    } else {
        playMusic(mpqPath);
    }
}

void MusicManager::update(float deltaTime) {
    // Check if player process is still running
    if (playerPid != INVALID_PROCESS) {
        if (!platform::isProcessRunning(playerPid)) {
            playing = false;
        }
    }

    // Handle crossfade
    if (crossfading) {
        fadeTimer += deltaTime;
        if (fadeTimer >= fadeDuration * 0.3f) {
            // Start new track after brief pause
            crossfading = false;
            playMusic(pendingTrack);
            pendingTrack.clear();
        }
    }
}

void MusicManager::stopCurrentProcess() {
    if (playerPid != INVALID_PROCESS) {
        platform::killProcess(playerPid);
        playing = false;
    }
}

} // namespace audio
} // namespace wowee
