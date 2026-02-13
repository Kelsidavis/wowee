#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace wowee {
namespace game {

/**
 * Identifies a WoW expansion for protocol/asset selection.
 */
struct ExpansionProfile {
    std::string id;            // "classic", "tbc", "wotlk", "cata"
    std::string name;          // "Wrath of the Lich King"
    std::string shortName;     // "WotLK"
    uint8_t majorVersion = 0;
    uint8_t minorVersion = 0;
    uint8_t patchVersion = 0;
    uint16_t build = 0;
    uint8_t protocolVersion = 0; // SRP auth protocol version byte
    std::string dataPath;      // Absolute path to expansion data dir
    uint32_t maxLevel = 60;
    std::vector<uint32_t> races;
    std::vector<uint32_t> classes;

    std::string versionString() const;  // e.g. "3.3.5a"
};

/**
 * Scans Data/expansions/ for available expansion profiles and manages the active selection.
 */
class ExpansionRegistry {
public:
    /**
     * Scan dataRoot/expansions/ for expansion.json files.
     * @param dataRoot Path to Data/ directory (e.g. "./Data")
     * @return Number of profiles discovered
     */
    size_t initialize(const std::string& dataRoot);

    /** All discovered profiles. */
    const std::vector<ExpansionProfile>& getAllProfiles() const { return profiles_; }

    /** Lookup by id (e.g. "wotlk"). Returns nullptr if not found. */
    const ExpansionProfile* getProfile(const std::string& id) const;

    /** Set the active expansion. Returns false if id not found. */
    bool setActive(const std::string& id);

    /** Get the active expansion profile. Never null after successful initialize(). */
    const ExpansionProfile* getActive() const;

    /** Convenience: active expansion id. Empty if none. */
    const std::string& getActiveId() const { return activeId_; }

private:
    std::vector<ExpansionProfile> profiles_;
    std::string activeId_;

    bool loadProfile(const std::string& jsonPath, const std::string& dirPath);
};

} // namespace game
} // namespace wowee
