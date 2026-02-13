#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace wowee {
namespace pipeline {

class AssetManager;

/**
 * Metadata for a single HD texture pack on disk.
 *
 * Each pack lives in Data/hd/<packDir>/ and contains:
 *   pack.json      - metadata (id, name, group, compatible expansions, size)
 *   manifest.json  - standard asset manifest with HD override textures
 *   assets/        - the actual HD files
 */
struct HDPack {
    std::string id;                         // Unique identifier (e.g. "character_hd")
    std::string name;                       // Human-readable name
    std::string group;                      // Grouping label (e.g. "Character", "Terrain")
    std::vector<std::string> expansions;    // Compatible expansion IDs
    uint32_t totalSizeMB = 0;              // Approximate total size on disk
    std::string manifestPath;              // Full path to manifest.json
    std::string packDir;                   // Full path to pack directory
    bool enabled = false;                  // User-toggled enable state
};

/**
 * HDPackManager - discovers, manages, and wires HD texture packs.
 *
 * Scans Data/hd/ subdirectories for pack.json files. Each pack can be
 * enabled/disabled via setPackEnabled(). Enabled packs are wired into
 * AssetManager as high-priority overlay manifests so that HD textures
 * override the base expansion assets transparently.
 */
class HDPackManager {
public:
    HDPackManager() = default;

    /**
     * Scan the HD root directory for available packs.
     * @param hdRootPath Path to Data/hd/ directory
     */
    void initialize(const std::string& hdRootPath);

    /**
     * Get all discovered packs.
     */
    const std::vector<HDPack>& getAllPacks() const { return packs_; }

    /**
     * Get packs compatible with a specific expansion.
     */
    std::vector<const HDPack*> getPacksForExpansion(const std::string& expansionId) const;

    /**
     * Enable or disable a pack. Persists state in enabledPacks_ map.
     */
    void setPackEnabled(const std::string& packId, bool enabled);

    /**
     * Check if a pack is enabled.
     */
    bool isPackEnabled(const std::string& packId) const;

    /**
     * Apply enabled packs as overlays to the asset manager.
     * Removes previously applied overlays and re-adds enabled ones.
     */
    void applyToAssetManager(AssetManager* assetManager, const std::string& expansionId);

    /**
     * Save enabled pack state to a settings file.
     */
    void saveSettings(const std::string& settingsPath) const;

    /**
     * Load enabled pack state from a settings file.
     */
    void loadSettings(const std::string& settingsPath);

private:
    std::vector<HDPack> packs_;
    std::unordered_map<std::string, bool> enabledState_;  // packId → enabled

    // Overlay IDs currently applied to AssetManager (for removal on re-apply)
    std::vector<std::string> appliedOverlayIds_;

    static constexpr int HD_OVERLAY_PRIORITY_BASE = 100;  // High priority, above expansion base
};

} // namespace pipeline
} // namespace wowee
