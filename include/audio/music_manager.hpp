#pragma once

#include <string>

namespace wowee {
namespace pipeline { class AssetManager; }

namespace audio {

class MusicManager {
public:
    MusicManager();
    ~MusicManager();

    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    void playMusic(const std::string& mpqPath, bool loop = true);
    void playFilePath(const std::string& filePath, bool loop = true);
    void stopMusic(float fadeMs = 2000.0f);
    void crossfadeTo(const std::string& mpqPath, float fadeMs = 3000.0f);
    void update(float deltaTime);
    void setVolume(int volume);
    int getVolume() const { return volumePercent; }

    bool isPlaying() const { return playing; }
    bool isInitialized() const { return assetManager != nullptr; }
    const std::string& getCurrentTrack() const { return currentTrack; }

private:
    pipeline::AssetManager* assetManager = nullptr;
    std::string currentTrack;
    bool currentTrackIsFile = false;
    bool playing = false;
    int volumePercent = 30;

    // Crossfade state
    bool crossfading = false;
    std::string pendingTrack;
    float fadeTimer = 0.0f;
    float fadeDuration = 0.0f;
};

} // namespace audio
} // namespace wowee
