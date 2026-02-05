#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace wowee::core::coords {

inline constexpr float TILE_SIZE = 533.33333f;
inline constexpr float ZEROPOINT = 32.0f * TILE_SIZE;

// ---- Canonical WoW world coordinate system (per-map) ----
//   +X = North,  +Y = West,  +Z = Up (height)
//   Origin (0,0,0) is the center of the 64x64 tile grid.
//   Full extent: ±17066.66656 in X and Y.
//
// ---- Engine rendering coordinate system ----
//   renderX = wowY (west),  renderY = wowX (north),  renderZ = wowZ (up)
//   Terrain vertices (MCNK) are stored directly in this space.
//
// ---- ADT file placement coordinate system ----
//   Used by MDDF (doodads) and MODF (WMOs) records in ADT files.
//   Range [0, 34133.333] with center at ZEROPOINT (17066.666).
//   adtY = height; adtX/adtZ are horizontal.

// Convert between canonical WoW and engine rendering coordinates (just swap X/Y).
inline glm::vec3 canonicalToRender(const glm::vec3& wow) {
    return glm::vec3(wow.y, wow.x, wow.z);
}

inline glm::vec3 renderToCanonical(const glm::vec3& render) {
    return glm::vec3(render.y, render.x, render.z);
}

// ADT file placement data (MDDF/MODF) -> engine rendering coordinates.
inline glm::vec3 adtToWorld(float adtX, float adtY, float adtZ) {
    return glm::vec3(
        -(adtZ - ZEROPOINT),   // renderX = ZP - adtZ  (= wowY)
        -(adtX - ZEROPOINT),   // renderY = ZP - adtX  (= wowX)
        adtY                    // renderZ = adtY       (= wowZ)
    );
}

inline glm::vec3 adtToWorld(const glm::vec3& adt) {
    return adtToWorld(adt.x, adt.y, adt.z);
}

// Engine rendering coordinates -> ADT file placement data.
inline glm::vec3 worldToAdt(float renderX, float renderY, float renderZ) {
    return glm::vec3(
        ZEROPOINT - renderY,   // adtX = ZP - renderY  (= ZP - wowX)
        renderZ,               // adtY = renderZ       (= wowZ, height)
        ZEROPOINT - renderX    // adtZ = ZP - renderX  (= ZP - wowY)
    );
}

inline glm::vec3 worldToAdt(const glm::vec3& world) {
    return worldToAdt(world.x, world.y, world.z);
}

// Engine rendering coordinates -> ADT tile indices.
// Returns (tileX, tileY) matching ADT filename: Map_{tileX}_{tileY}.adt
// Uses canonical formula: tileN = floor(32 - wowN / TILE_SIZE)
inline std::pair<int, int> worldToTile(float renderX, float renderY) {
    // renderY = wowX (north), renderX = wowY (west)
    int tileX = static_cast<int>(std::floor(32.0f - renderY / TILE_SIZE));
    int tileY = static_cast<int>(std::floor(32.0f - renderX / TILE_SIZE));
    tileX = std::clamp(tileX, 0, 63);
    tileY = std::clamp(tileY, 0, 63);
    return {tileX, tileY};
}

// Canonical WoW coordinates -> ADT tile indices.
inline std::pair<int, int> canonicalToTile(float wowX, float wowY) {
    int tileX = static_cast<int>(std::floor(32.0f - wowX / TILE_SIZE));
    int tileY = static_cast<int>(std::floor(32.0f - wowY / TILE_SIZE));
    tileX = std::clamp(tileX, 0, 63);
    tileY = std::clamp(tileY, 0, 63);
    return {tileX, tileY};
}

} // namespace wowee::core::coords
