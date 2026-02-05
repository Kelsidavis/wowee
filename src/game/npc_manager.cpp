#include "game/npc_manager.hpp"
#include "game/entity.hpp"
#include "core/coordinates.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "rendering/character_renderer.hpp"
#include "core/logger.hpp"
#include <random>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace wowee {
namespace game {

// Random emote animation IDs (humanoid only)
static const uint32_t EMOTE_ANIMS[] = { 60, 66, 67, 70 }; // Talk, Bow, Wave, Laugh
static constexpr int NUM_EMOTE_ANIMS = 4;

static float randomFloat(float lo, float hi) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

static std::string toLowerStr(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Look up texture variants for a creature M2 using CreatureDisplayInfo.dbc
// Returns up to 3 texture variant names (for type 1, 2, 3 texture slots)
static std::vector<std::string> lookupTextureVariants(
        pipeline::AssetManager* am, const std::string& m2Path) {
    std::vector<std::string> variants;

    auto modelDataDbc = am->loadDBC("CreatureModelData.dbc");
    auto displayInfoDbc = am->loadDBC("CreatureDisplayInfo.dbc");
    if (!modelDataDbc || !displayInfoDbc) return variants;

    // CreatureModelData stores .mdx paths; convert our .m2 path for matching
    std::string mdxPath = m2Path;
    if (mdxPath.size() > 3) {
        mdxPath = mdxPath.substr(0, mdxPath.size() - 3) + ".mdx";
    }
    std::string mdxLower = toLowerStr(mdxPath);

    // Find model ID from CreatureModelData (col 0 = ID, col 2 = modelName)
    uint32_t creatureModelId = 0;
    for (uint32_t r = 0; r < modelDataDbc->getRecordCount(); r++) {
        std::string dbcModel = modelDataDbc->getString(r, 2);
        if (toLowerStr(dbcModel) == mdxLower) {
            creatureModelId = modelDataDbc->getUInt32(r, 0);
            LOG_INFO("NpcManager: DBC match for '", m2Path,
                     "' -> CreatureModelData ID ", creatureModelId);
            break;
        }
    }
    if (creatureModelId == 0) return variants;

    // Find first CreatureDisplayInfo entry for this model
    // Col 0=ID, 1=ModelID, 6=TextureVariation_1, 7=TextureVariation_2, 8=TextureVariation_3
    for (uint32_t r = 0; r < displayInfoDbc->getRecordCount(); r++) {
        if (displayInfoDbc->getUInt32(r, 1) == creatureModelId) {
            std::string v1 = displayInfoDbc->getString(r, 6);
            std::string v2 = displayInfoDbc->getString(r, 7);
            std::string v3 = displayInfoDbc->getString(r, 8);
            if (!v1.empty()) variants.push_back(v1);
            if (!v2.empty()) variants.push_back(v2);
            if (!v3.empty()) variants.push_back(v3);
            LOG_INFO("NpcManager: DisplayInfo textures: '", v1, "', '", v2, "', '", v3, "'");
            break;
        }
    }
    return variants;
}

void NpcManager::loadCreatureModel(pipeline::AssetManager* am,
                                    rendering::CharacterRenderer* cr,
                                    const std::string& m2Path,
                                    uint32_t modelId) {
    auto m2Data = am->readFile(m2Path);
    if (m2Data.empty()) {
        LOG_WARNING("NpcManager: failed to read M2 file: ", m2Path);
        return;
    }

    auto model = pipeline::M2Loader::load(m2Data);

    // Derive skin path: replace .m2 with 00.skin
    std::string skinPath = m2Path;
    if (skinPath.size() > 3) {
        skinPath = skinPath.substr(0, skinPath.size() - 3) + "00.skin";
    }
    auto skinData = am->readFile(skinPath);
    if (!skinData.empty()) {
        pipeline::M2Loader::loadSkin(skinData, model);
    }

    if (!model.isValid()) {
        LOG_WARNING("NpcManager: invalid model: ", m2Path);
        return;
    }

    // Load external .anim files for sequences without flag 0x20
    std::string basePath = m2Path.substr(0, m2Path.size() - 3); // remove ".m2"
    for (uint32_t si = 0; si < model.sequences.size(); si++) {
        if (!(model.sequences[si].flags & 0x20)) {
            char animFileName[256];
            snprintf(animFileName, sizeof(animFileName),
                "%s%04u-%02u.anim",
                basePath.c_str(),
                model.sequences[si].id,
                model.sequences[si].variationIndex);
            auto animFileData = am->readFile(animFileName);
            if (!animFileData.empty()) {
                pipeline::M2Loader::loadAnimFile(m2Data, animFileData, si, model);
            }
        }
    }

    // --- Resolve creature skin textures ---
    // Extract model directory: "Creature\Wolf\" from "Creature\Wolf\Wolf.m2"
    size_t lastSlash = m2Path.find_last_of("\\/");
    std::string modelDir = (lastSlash != std::string::npos)
                           ? m2Path.substr(0, lastSlash + 1) : "";

    // Extract model base name: "Wolf" from "Creature\Wolf\Wolf.m2"
    std::string modelFileName = (lastSlash != std::string::npos)
                                ? m2Path.substr(lastSlash + 1) : m2Path;
    std::string modelBaseName = modelFileName.substr(0, modelFileName.size() - 3); // remove ".m2"

    // Log existing texture info
    for (size_t ti = 0; ti < model.textures.size(); ti++) {
        LOG_INFO("NpcManager: ", m2Path, " tex[", ti, "] type=",
                 model.textures[ti].type, " file='", model.textures[ti].filename, "'");
    }

    // Check if any textures need resolution
    // Type 11 = creature skin 1, type 12 = creature skin 2, type 13 = creature skin 3
    // Type 1 = character body skin (also possible on some creature models)
    auto needsResolve = [](uint32_t t) {
        return t == 11 || t == 12 || t == 13 || t == 1 || t == 2 || t == 3;
    };

    bool needsVariants = false;
    for (const auto& tex : model.textures) {
        if (needsResolve(tex.type) && tex.filename.empty()) {
            needsVariants = true;
            break;
        }
    }

    if (needsVariants) {
        // Try DBC-based lookup first
        auto variants = lookupTextureVariants(am, m2Path);

        // Fill in unresolved textures from DBC variants
        // Creature skin types map: type 11 -> variant[0], type 12 -> variant[1], type 13 -> variant[2]
        // Also type 1 -> variant[0] as fallback
        for (auto& tex : model.textures) {
            if (!needsResolve(tex.type) || !tex.filename.empty()) continue;

            // Determine which variant index this texture type maps to
            size_t varIdx = 0;
            if (tex.type == 11 || tex.type == 1) varIdx = 0;
            else if (tex.type == 12 || tex.type == 2) varIdx = 1;
            else if (tex.type == 13 || tex.type == 3) varIdx = 2;

            std::string resolved;

            if (varIdx < variants.size() && !variants[varIdx].empty()) {
                // DBC variant: <ModelDir>\<Variant>.blp
                resolved = modelDir + variants[varIdx] + ".blp";
                if (!am->fileExists(resolved)) {
                    LOG_WARNING("NpcManager: DBC texture not found: ", resolved);
                    resolved.clear();
                }
            }

            // Fallback heuristics if DBC didn't provide a texture
            if (resolved.empty()) {
                // Try <ModelDir>\<ModelName>Skin.blp
                std::string skinTry = modelDir + modelBaseName + "Skin.blp";
                if (am->fileExists(skinTry)) {
                    resolved = skinTry;
                } else {
                    // Try <ModelDir>\<ModelName>.blp
                    std::string altTry = modelDir + modelBaseName + ".blp";
                    if (am->fileExists(altTry)) {
                        resolved = altTry;
                    }
                }
            }

            if (!resolved.empty()) {
                tex.filename = resolved;
                LOG_INFO("NpcManager: resolved type-", tex.type,
                         " texture -> '", resolved, "'");
            } else {
                LOG_WARNING("NpcManager: could not resolve type-", tex.type,
                            " texture for ", m2Path);
            }
        }
    }

    cr->loadModel(model, modelId);
    LOG_INFO("NpcManager: loaded model id=", modelId, " path=", m2Path,
             " verts=", model.vertices.size(), " bones=", model.bones.size(),
             " anims=", model.sequences.size(), " textures=", model.textures.size());
}

void NpcManager::initialize(pipeline::AssetManager* am,
                             rendering::CharacterRenderer* cr,
                             EntityManager& em,
                             const glm::vec3& playerSpawnGL) {
    if (!am || !am->isInitialized() || !cr) {
        LOG_WARNING("NpcManager: cannot initialize — missing AssetManager or CharacterRenderer");
        return;
    }

    // Define spawn table: NPC positions are offsets from player spawn in GL coords
    struct SpawnEntry {
        const char* name;
        const char* m2Path;
        uint32_t level;
        uint32_t health;
        float offsetX;  // GL X offset from player
        float offsetY;  // GL Y offset from player
        float rotation;
        float scale;
        bool isCritter;
    };

    static const SpawnEntry spawnTable[] = {
        // Guards
        { "Stormwind Guard", "Creature\\HumanMaleGuard\\HumanMaleGuard.m2",
          60, 42000, -15.0f, 10.0f, 0.0f, 1.0f, false },
        { "Stormwind Guard", "Creature\\HumanMaleGuard\\HumanMaleGuard.m2",
          60, 42000, 20.0f, -5.0f, 2.3f, 1.0f, false },
        { "Stormwind Guard", "Creature\\HumanMaleGuard\\HumanMaleGuard.m2",
          60, 42000, -25.0f, -15.0f, 1.0f, 1.0f, false },

        // Citizens
        { "Stormwind Citizen", "Creature\\HumanMalePeasant\\HumanMalePeasant.m2",
          5, 1200, 12.0f, 18.0f, 3.5f, 1.0f, false },
        { "Stormwind Citizen", "Creature\\HumanMalePeasant\\HumanMalePeasant.m2",
          5, 1200, -8.0f, -22.0f, 5.0f, 1.0f, false },
        { "Stormwind Citizen", "Creature\\HumanFemalePeasant\\HumanFemalePeasant.m2",
          5, 1200, 30.0f, 8.0f, 1.8f, 1.0f, false },
        { "Stormwind Citizen", "Creature\\HumanFemalePeasant\\HumanFemalePeasant.m2",
          5, 1200, -18.0f, 25.0f, 4.2f, 1.0f, false },

        // Critters
        { "Wolf", "Creature\\Wolf\\Wolf.m2",
          1, 42, 35.0f, -20.0f, 0.7f, 1.0f, true },
        { "Wolf", "Creature\\Wolf\\Wolf.m2",
          1, 42, 40.0f, -15.0f, 1.2f, 1.0f, true },
        { "Chicken", "Creature\\Chicken\\Chicken.m2",
          1, 10, -10.0f, 30.0f, 2.0f, 1.0f, true },
        { "Chicken", "Creature\\Chicken\\Chicken.m2",
          1, 10, -12.0f, 33.0f, 3.8f, 1.0f, true },
        { "Cat", "Creature\\Cat\\Cat.m2",
          1, 42, 5.0f, -35.0f, 4.5f, 1.0f, true },
        { "Deer", "Creature\\Deer\\Deer.m2",
          1, 42, -35.0f, -30.0f, 0.3f, 1.0f, true },
    };

    constexpr size_t spawnCount = sizeof(spawnTable) / sizeof(spawnTable[0]);

    // Load each unique M2 model once
    for (size_t i = 0; i < spawnCount; i++) {
        const std::string path(spawnTable[i].m2Path);
        if (loadedModels.find(path) == loadedModels.end()) {
            uint32_t mid = nextModelId++;
            loadCreatureModel(am, cr, path, mid);
            loadedModels[path] = mid;
        }
    }

    // Spawn each NPC instance
    for (size_t i = 0; i < spawnCount; i++) {
        const auto& s = spawnTable[i];
        const std::string path(s.m2Path);

        auto it = loadedModels.find(path);
        if (it == loadedModels.end()) continue; // model failed to load

        uint32_t modelId = it->second;

        // GL position: offset from player spawn
        glm::vec3 glPos = playerSpawnGL + glm::vec3(s.offsetX, s.offsetY, 0.0f);

        // Create render instance
        uint32_t instanceId = cr->createInstance(modelId, glPos,
            glm::vec3(0.0f, 0.0f, s.rotation), s.scale);
        if (instanceId == 0) {
            LOG_WARNING("NpcManager: failed to create instance for ", s.name);
            continue;
        }

        // Play idle animation (anim ID 0)
        cr->playAnimation(instanceId, 0, true);

        // Assign unique GUID
        uint64_t guid = nextGuid++;

        // Create entity in EntityManager
        auto unit = std::make_shared<Unit>(guid);
        unit->setName(s.name);
        unit->setLevel(s.level);
        unit->setHealth(s.health);
        unit->setMaxHealth(s.health);

        // Store canonical WoW coordinates for targeting/server compatibility
        glm::vec3 canonical = core::coords::renderToCanonical(glPos);
        unit->setPosition(canonical.x, canonical.y, canonical.z, s.rotation);

        em.addEntity(guid, unit);

        // Track NPC instance
        NpcInstance npc{};
        npc.guid = guid;
        npc.renderInstanceId = instanceId;
        npc.emoteTimer = randomFloat(5.0f, 15.0f);
        npc.emoteEndTimer = 0.0f;
        npc.isEmoting = false;
        npc.isCritter = s.isCritter;
        npcs.push_back(npc);

        LOG_INFO("NpcManager: spawned '", s.name, "' guid=0x", std::hex, guid, std::dec,
                 " at GL(", glPos.x, ",", glPos.y, ",", glPos.z, ")");
    }

    LOG_INFO("NpcManager: initialized ", npcs.size(), " NPCs with ",
             loadedModels.size(), " unique models");
}

void NpcManager::update(float deltaTime, rendering::CharacterRenderer* cr) {
    if (!cr) return;

    for (auto& npc : npcs) {
        // Critters just idle — no emotes
        if (npc.isCritter) continue;

        if (npc.isEmoting) {
            npc.emoteEndTimer -= deltaTime;
            if (npc.emoteEndTimer <= 0.0f) {
                // Return to idle
                cr->playAnimation(npc.renderInstanceId, 0, true);
                npc.isEmoting = false;
                npc.emoteTimer = randomFloat(5.0f, 15.0f);
            }
        } else {
            npc.emoteTimer -= deltaTime;
            if (npc.emoteTimer <= 0.0f) {
                // Play random emote
                int idx = static_cast<int>(randomFloat(0.0f, static_cast<float>(NUM_EMOTE_ANIMS) - 0.01f));
                uint32_t emoteAnim = EMOTE_ANIMS[idx];
                cr->playAnimation(npc.renderInstanceId, emoteAnim, false);
                npc.isEmoting = true;
                npc.emoteEndTimer = randomFloat(2.0f, 4.0f);
            }
        }
    }
}

} // namespace game
} // namespace wowee
