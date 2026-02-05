#pragma once

#include <glm/glm.hpp>

namespace wowee::core {

struct SpawnPreset {
    const char* key;
    const char* label;
    const char* mapName;        // Map name for ADT paths (e.g., "Azeroth")
    glm::vec3 spawnCanonical;   // Canonical WoW coords: +X=North, +Y=West, +Z=Up
    float yawDeg;
    float pitchDeg;
};

// Spawn positions in canonical WoW world coordinates (X=north, Y=west, Z=up).
// Tile is computed from position via: tileN = floor(32 - wowN / 533.33333)
inline const SpawnPreset SPAWN_PRESETS[] = {
    {"goldshire",  "Goldshire",      "Azeroth", glm::vec3(   62.0f, -9464.0f, 200.0f), 0.0f, -5.0f},
    {"stormwind",  "Stormwind Gate", "Azeroth", glm::vec3(  425.0f, -9176.0f, 120.0f), 35.0f, -8.0f},
    {"ironforge",  "Ironforge",      "Azeroth", glm::vec3( -882.0f, -4981.0f, 510.0f), -20.0f, -8.0f},
    {"westfall",   "Westfall",       "Azeroth", glm::vec3( 1215.0f,-10440.0f,  80.0f), 10.0f, -8.0f},
};

inline constexpr int SPAWN_PRESET_COUNT = 4;

} // namespace wowee::core
