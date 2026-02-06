#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterRenderer; }
namespace rendering { class TerrainManager; }
namespace game {

class EntityManager;

struct NpcSpawnDef {
    std::string mapName;
    uint32_t entry = 0;
    std::string name;
    std::string m2Path;
    uint32_t level;
    uint32_t health;
    glm::vec3 canonicalPosition; // WoW canonical coords (+X north, +Y west, +Z up)
    bool inputIsServerCoords = false; // if true, input XYZ are server/wire order
    float rotation;          // radians around Z
    float scale;
    bool isCritter;          // critters don't do humanoid emotes
    uint32_t faction = 0;    // faction template ID from creature_template
    uint32_t npcFlags = 0;   // NPC interaction flags from creature_template
};

struct NpcInstance {
    uint64_t guid;
    uint32_t renderInstanceId;
    float emoteTimer;        // countdown to next random emote
    float emoteEndTimer;     // countdown until emote animation finishes
    bool isEmoting;
    bool isCritter;
};

class NpcManager {
public:
    void clear(rendering::CharacterRenderer* cr, EntityManager* em);
    void initialize(pipeline::AssetManager* am,
                    rendering::CharacterRenderer* cr,
                    EntityManager& em,
                    const std::string& mapName,
                    const glm::vec3& playerCanonical,
                    const rendering::TerrainManager* terrainManager);
    void update(float deltaTime, rendering::CharacterRenderer* cr);

    uint32_t findRenderInstanceId(uint64_t guid) const;

private:
    std::vector<NpcSpawnDef> loadSpawnDefsFromFile(const std::string& path) const;
    std::vector<NpcSpawnDef> loadSpawnDefsFromAzerothCoreDb(
        const std::string& basePath,
        const std::string& mapName,
        const glm::vec3& playerCanonical,
        pipeline::AssetManager* am) const;

    void loadCreatureModel(pipeline::AssetManager* am,
                           rendering::CharacterRenderer* cr,
                           const std::string& m2Path,
                           uint32_t modelId);

    std::vector<NpcInstance> npcs;
    std::unordered_map<std::string, uint32_t> loadedModels; // path -> modelId
    uint64_t nextGuid = 0xF1300000DEAD0001ULL;
    uint32_t nextModelId = 100;
};

} // namespace game
} // namespace wowee
