#include "game/npc_manager.hpp"
#include "game/entity.hpp"
#include "core/coordinates.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/terrain_manager.hpp"
#include "core/logger.hpp"
#include <random>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>

namespace wowee {
namespace game {

void NpcManager::clear(rendering::CharacterRenderer* cr, EntityManager* em) {
    for (const auto& npc : npcs) {
        if (cr) {
            cr->removeInstance(npc.renderInstanceId);
        }
        if (em) {
            em->removeEntity(npc.guid);
        }
    }
    npcs.clear();
    loadedModels.clear();
}

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

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

static std::string normalizeMapName(const std::string& raw) {
    std::string n = toLowerStr(trim(raw));
    n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == '_'; }), n.end());
    return n;
}

static bool mapNamesEquivalent(const std::string& a, const std::string& b) {
    std::string na = normalizeMapName(a);
    std::string nb = normalizeMapName(b);
    if (na == nb) return true;
    // Azeroth world aliases seen across systems/UI.
    auto isAzerothAlias = [](const std::string& n) {
        return n == "azeroth" || n == "easternkingdoms" || n == "easternkingdom";
    };
    return isAzerothAlias(na) && isAzerothAlias(nb);
}

static bool parseVec2Csv(const char* raw, float& x, float& y) {
    if (!raw || !*raw) return false;
    std::string s(raw);
    std::replace(s.begin(), s.end(), ';', ',');
    std::stringstream ss(s);
    std::string a, b;
    if (!std::getline(ss, a, ',')) return false;
    if (!std::getline(ss, b, ',')) return false;
    try {
        x = std::stof(trim(a));
        y = std::stof(trim(b));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

static bool parseFloatEnv(const char* raw, float& out) {
    if (!raw || !*raw) return false;
    try {
        out = std::stof(trim(raw));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

static int mapNameToId(const std::string& mapName) {
    std::string n = normalizeMapName(mapName);
    if (n == "azeroth" || n == "easternkingdoms" || n == "easternkingdom") return 0;
    if (n == "kalimdor") return 1;
    if (n == "outland" || n == "expansion01") return 530;
    if (n == "northrend") return 571;
    return 0;
}

static bool parseInsertTuples(const std::string& line, std::vector<std::string>& outTuples) {
    outTuples.clear();
    size_t valuesPos = line.find("VALUES");
    if (valuesPos == std::string::npos) valuesPos = line.find("values");
    if (valuesPos == std::string::npos) return false;

    bool inQuote = false;
    int depth = 0;
    size_t tupleStart = std::string::npos;
    for (size_t i = valuesPos; i < line.size(); i++) {
        char c = line[i];
        if (c == '\'' && (i == 0 || line[i - 1] != '\\')) inQuote = !inQuote;
        if (inQuote) continue;
        if (c == '(') {
            if (depth == 0) tupleStart = i + 1;
            depth++;
        } else if (c == ')') {
            depth--;
            if (depth == 0 && tupleStart != std::string::npos && i > tupleStart) {
                outTuples.push_back(line.substr(tupleStart, i - tupleStart));
                tupleStart = std::string::npos;
            }
        }
    }
    return !outTuples.empty();
}

static std::vector<std::string> splitCsvTuple(const std::string& tuple) {
    std::vector<std::string> cols;
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < tuple.size(); i++) {
        char c = tuple[i];
        if (c == '\'' && (i == 0 || tuple[i - 1] != '\\')) {
            inQuote = !inQuote;
            cur.push_back(c);
            continue;
        }
        if (c == ',' && !inQuote) {
            cols.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) cols.push_back(trim(cur));
    return cols;
}

static std::string unquoteSqlString(const std::string& s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

static glm::vec3 toCanonicalSpawn(const NpcSpawnDef& s, bool swapXY, float rotDeg,
                                  float pivotX, float pivotY, float dx, float dy) {
    glm::vec3 canonical = s.inputIsServerCoords
        ? core::coords::serverToCanonical(s.canonicalPosition)
        : s.canonicalPosition;
    if (swapXY) std::swap(canonical.x, canonical.y);

    if (std::abs(rotDeg) > 0.001f) {
        float rad = rotDeg * (3.1415926535f / 180.0f);
        float c = std::cos(rad);
        float s = std::sin(rad);
        float x = canonical.x - pivotX;
        float y = canonical.y - pivotY;
        canonical.x = pivotX + x * c - y * s;
        canonical.y = pivotY + x * s + y * c;
    }

    canonical.x += dx;
    canonical.y += dy;
    return canonical;
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

std::vector<NpcSpawnDef> NpcManager::loadSpawnDefsFromFile(const std::string& path) const {
    std::vector<NpcSpawnDef> out;
    std::string resolvedPath;
    const std::string candidates[] = {
        path,
        "./" + path,
        "../" + path,
        "../../" + path,
        "../../../" + path
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) {
            resolvedPath = c;
            break;
        }
    }
    if (resolvedPath.empty()) {
        // Try relative to executable location.
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            std::filesystem::path dir = exe.parent_path();
            for (int i = 0; i < 5 && !dir.empty(); i++) {
                std::filesystem::path candidate = dir / path;
                if (std::filesystem::exists(candidate)) {
                    resolvedPath = candidate.string();
                    break;
                }
                dir = dir.parent_path();
            }
        }
    }

    if (resolvedPath.empty()) {
        LOG_WARNING("NpcManager: spawn CSV not found at ", path, " (or nearby relative paths)");
        return out;
    }

    std::ifstream in(resolvedPath);
    if (!in.is_open()) return out;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            cols.push_back(trim(tok));
        }

        if (cols.size() != 11 && cols.size() != 12) {
            LOG_WARNING("NpcManager: bad NPC CSV row at ", resolvedPath, ":", lineNo,
                        " (expected 11 or 12 columns)");
            continue;
        }

        try {
            NpcSpawnDef def;
            def.mapName = cols[0];
            def.name = cols[1];
            def.m2Path = cols[2];
            def.level = static_cast<uint32_t>(std::stoul(cols[3]));
            def.health = static_cast<uint32_t>(std::stoul(cols[4]));
            def.canonicalPosition.x = std::stof(cols[5]);
            def.canonicalPosition.y = std::stof(cols[6]);
            def.canonicalPosition.z = std::stof(cols[7]);
            def.rotation = std::stof(cols[8]);
            def.scale = std::stof(cols[9]);
            def.isCritter = (cols[10] == "1" || toLowerStr(cols[10]) == "true");
            if (cols.size() == 12) {
                std::string space = toLowerStr(cols[11]);
                def.inputIsServerCoords = (space == "server" || space == "wire");
            }

            if (def.mapName.empty() || def.name.empty() || def.m2Path.empty()) continue;
            out.push_back(std::move(def));
        } catch (const std::exception&) {
                LOG_WARNING("NpcManager: failed parsing NPC CSV row at ", resolvedPath, ":", lineNo);
        }
    }

    LOG_INFO("NpcManager: loaded ", out.size(), " spawn defs from ", resolvedPath);

    return out;
}

std::vector<NpcSpawnDef> NpcManager::loadSpawnDefsFromAzerothCoreDb(
    const std::string& basePath,
    const std::string& mapName,
    const glm::vec3& playerCanonical,
    pipeline::AssetManager* am) const {
    std::vector<NpcSpawnDef> out;
    if (!am) return out;

    std::filesystem::path base(basePath);
    std::filesystem::path creaturePath = base / "creature.sql";
    std::filesystem::path tmplPath = base / "creature_template.sql";
    if (!std::filesystem::exists(creaturePath) || !std::filesystem::exists(tmplPath)) {
        // Allow passing .../sql or repo root as WOW_DB_BASE_PATH.
        std::filesystem::path alt = base / "base";
        if (std::filesystem::exists(alt / "creature.sql") && std::filesystem::exists(alt / "creature_template.sql")) {
            base = alt;
            creaturePath = base / "creature.sql";
            tmplPath = base / "creature_template.sql";
        } else {
            alt = base / "sql" / "base";
            if (std::filesystem::exists(alt / "creature.sql") && std::filesystem::exists(alt / "creature_template.sql")) {
                base = alt;
                creaturePath = base / "creature.sql";
                tmplPath = base / "creature_template.sql";
            }
        }
    }
    if (!std::filesystem::exists(creaturePath) || !std::filesystem::exists(tmplPath)) {
        return out;
    }

    struct TemplateRow {
        std::string name;
        uint32_t level = 1;
        uint32_t health = 100;
        std::string m2Path;
        uint32_t faction = 0;
        uint32_t npcFlags = 0;
    };
    std::unordered_map<uint32_t, TemplateRow> templates;

    // Build displayId -> modelId lookup.
    std::unordered_map<uint32_t, uint32_t> displayToModel;
    if (auto cdi = am->loadDBC("CreatureDisplayInfo.dbc"); cdi && cdi->isLoaded()) {
        for (uint32_t i = 0; i < cdi->getRecordCount(); i++) {
            displayToModel[cdi->getUInt32(i, 0)] = cdi->getUInt32(i, 1);
        }
    }
    std::unordered_map<uint32_t, std::string> modelToPath;
    if (auto cmd = am->loadDBC("CreatureModelData.dbc"); cmd && cmd->isLoaded()) {
        for (uint32_t i = 0; i < cmd->getRecordCount(); i++) {
            std::string mdx = cmd->getString(i, 2);
            if (mdx.empty()) continue;
            std::string p = mdx;
            if (p.size() >= 4) p = p.substr(0, p.size() - 4) + ".m2";
            modelToPath[cmd->getUInt32(i, 0)] = p;
        }
    }

    auto processInsertStatements =
        [](std::ifstream& in, const std::function<bool(const std::vector<std::string>&)>& onTuple) {
            std::string line;
            std::string stmt;
            std::vector<std::string> tuples;
            while (std::getline(in, line)) {
                if (stmt.empty()) {
                    // Skip non-INSERT lines early.
                    if (line.find("INSERT INTO") == std::string::npos &&
                        line.find("insert into") == std::string::npos) {
                        continue;
                    }
                }
                if (!stmt.empty()) stmt.push_back('\n');
                stmt += line;
                if (line.find(';') == std::string::npos) continue;

                if (parseInsertTuples(stmt, tuples)) {
                    for (const auto& t : tuples) {
                        if (!onTuple(splitCsvTuple(t))) {
                            return;
                        }
                    }
                }
                stmt.clear();
            }
        };

    // Parse creature_template.sql: entry, modelid1(displayId), name, minlevel, faction, npcflag.
    {
        std::ifstream in(tmplPath);
        processInsertStatements(in, [&](const std::vector<std::string>& cols) {
                if (cols.size() < 19) return true;
                try {
                    uint32_t entry = static_cast<uint32_t>(std::stoul(cols[0]));
                    uint32_t displayId = static_cast<uint32_t>(std::stoul(cols[6]));
                    std::string name = unquoteSqlString(cols[10]);
                    uint32_t minLevel = static_cast<uint32_t>(std::stoul(cols[14]));
                    uint32_t faction = static_cast<uint32_t>(std::stoul(cols[17]));
                    uint32_t npcflag = static_cast<uint32_t>(std::stoul(cols[18]));
                    TemplateRow tr;
                    tr.name = name.empty() ? ("Creature " + std::to_string(entry)) : name;
                    tr.level = std::max(1u, minLevel);
                    tr.health = 150 + tr.level * 35;
                    tr.faction = faction;
                    tr.npcFlags = npcflag;
                    auto itModel = displayToModel.find(displayId);
                    if (itModel != displayToModel.end()) {
                        auto itPath = modelToPath.find(itModel->second);
                        if (itPath != modelToPath.end()) tr.m2Path = itPath->second;
                    }
                    templates[entry] = std::move(tr);
                } catch (const std::exception&) {
                }
                return true;
            });
    }

    int targetMap = mapNameToId(mapName);
    constexpr float kRadius = 2200.0f;
    constexpr size_t kMaxSpawns = 220;
    std::ifstream in(creaturePath);
    processInsertStatements(in, [&](const std::vector<std::string>& cols) {
            if (cols.size() < 16) return true;
            try {
                uint32_t entry = static_cast<uint32_t>(std::stoul(cols[1]));
                int mapId = static_cast<int>(std::stol(cols[2]));
                if (mapId != targetMap) return true;

                float sx = std::stof(cols[7]);
                float sy = std::stof(cols[8]);
                float sz = std::stof(cols[9]);
                float o = std::stof(cols[10]);
                uint32_t curhealth = static_cast<uint32_t>(std::stoul(cols[14]));

                // AzerothCore DB uses client/canonical coordinates.
                glm::vec3 canonical = glm::vec3(sx, sy, sz);
                float dx = canonical.x - playerCanonical.x;
                float dy = canonical.y - playerCanonical.y;
                if (dx * dx + dy * dy > kRadius * kRadius) return true;

            NpcSpawnDef def;
            def.mapName = mapName;
            auto it = templates.find(entry);
            if (it != templates.end()) {
                def.entry = entry;
                def.name = it->second.name;
                def.level = it->second.level;
                def.health = std::max(it->second.health, curhealth);
                def.m2Path = it->second.m2Path;
                def.faction = it->second.faction;
                def.npcFlags = it->second.npcFlags;
            } else {
                def.entry = entry;
                def.name = "Creature " + std::to_string(entry);
                def.level = 1;
                def.health = std::max(100u, curhealth);
            }
                if (def.m2Path.empty()) {
                    def.m2Path = "Creature\\HumanMalePeasant\\HumanMalePeasant.m2";
                }
                def.canonicalPosition = canonical;
                def.inputIsServerCoords = false;
                def.rotation = o;
                def.scale = 1.0f;
                def.isCritter = (def.level <= 1 || def.health <= 50);
                out.push_back(std::move(def));
                if (out.size() >= kMaxSpawns) return false;
            } catch (const std::exception&) {
            }
            return true;
        });

    LOG_INFO("NpcManager: loaded ", out.size(), " nearby creature spawns from AzerothCore DB at ", basePath);
    return out;
}

void NpcManager::initialize(pipeline::AssetManager* am,
                             rendering::CharacterRenderer* cr,
                             EntityManager& em,
                             const std::string& mapName,
                             const glm::vec3& playerCanonical,
                             const rendering::TerrainManager* terrainManager) {
    if (!am || !am->isInitialized() || !cr) {
        LOG_WARNING("NpcManager: cannot initialize — missing AssetManager or CharacterRenderer");
        return;
    }

    float globalDx = 0.0f;
    float globalDy = 0.0f;
    bool hasGlobalOffset = parseVec2Csv(std::getenv("WOW_NPC_OFFSET"), globalDx, globalDy);
    float globalRotDeg = 0.0f;
    parseFloatEnv(std::getenv("WOW_NPC_ROT_DEG"), globalRotDeg);
    bool swapXY = false;
    if (const char* swap = std::getenv("WOW_NPC_SWAP_XY")) {
        std::string v = toLowerStr(trim(swap));
        swapXY = (v == "1" || v == "true" || v == "yes");
    }
    float pivotX = playerCanonical.x;
    float pivotY = playerCanonical.y;
    parseVec2Csv(std::getenv("WOW_NPC_PIVOT"), pivotX, pivotY);

    if (hasGlobalOffset || swapXY || std::abs(globalRotDeg) > 0.001f) {
        LOG_INFO("NpcManager: transform overrides swapXY=", swapXY,
                 " rotDeg=", globalRotDeg,
                 " pivot=(", pivotX, ", ", pivotY, ")",
                 " offset=(", globalDx, ", ", globalDy, ")");
    }

    std::vector<NpcSpawnDef> spawnDefs;
    std::string dbBasePath;
    if (const char* dbBase = std::getenv("WOW_DB_BASE_PATH")) {
        dbBasePath = dbBase;
    } else if (std::filesystem::exists("assets/sql")) {
        dbBasePath = "assets/sql";
    }
    if (!dbBasePath.empty()) {
        auto dbDefs = loadSpawnDefsFromAzerothCoreDb(dbBasePath, mapName, playerCanonical, am);
        if (!dbDefs.empty()) spawnDefs = std::move(dbDefs);
    }
    if (spawnDefs.empty()) {
        LOG_WARNING("NpcManager: no spawn defs found (DB required for single-player)");
    }

    // Spawn only nearby placements on current map.
    std::vector<const NpcSpawnDef*> active;
    active.reserve(spawnDefs.size());
    constexpr float kSpawnRadius = 2200.0f;
    int mapSkipped = 0;
    for (const auto& s : spawnDefs) {
        if (!mapNamesEquivalent(mapName, s.mapName)) {
            mapSkipped++;
            continue;
        }
        glm::vec3 c = toCanonicalSpawn(s, swapXY, globalRotDeg, pivotX, pivotY, globalDx, globalDy);
        float distX = c.x - playerCanonical.x;
        float distY = c.y - playerCanonical.y;
        if (distX * distX + distY * distY > kSpawnRadius * kSpawnRadius) continue;
        active.push_back(&s);
    }

    if (active.empty()) {
        LOG_INFO("NpcManager: no static NPC placements near player on map ", mapName,
                 " (mapSkipped=", mapSkipped, ")");
        return;
    }

    // Load each unique M2 model once
    for (const auto* s : active) {
        const std::string path = s->m2Path;
        if (loadedModels.find(path) == loadedModels.end()) {
            uint32_t mid = nextModelId++;
            loadCreatureModel(am, cr, path, mid);
            loadedModels[path] = mid;
        }
    }

    // Build faction hostility lookup from FactionTemplate.dbc.
    // Player is Alliance (Human) — faction template 1, friendGroup includes Alliance mask.
    // A creature is hostile if its enemyGroup overlaps the player's friendGroup.
    std::unordered_map<uint32_t, bool> factionHostile; // factionTemplateId → hostile to player
    {
        // FactionTemplate.dbc columns (3.3.5a):
        //  0: ID, 1: Faction, 2: Flags, 3: FactionGroup, 4: FriendGroup, 5: EnemyGroup,
        //  6-9: Enemies[4], 10-13: Friends[4]
        uint32_t playerFriendGroup = 0;
        if (auto dbc = am->loadDBC("FactionTemplate.dbc"); dbc && dbc->isLoaded()) {
            // First pass: find player faction template (ID 1) friendGroup
            for (uint32_t i = 0; i < dbc->getRecordCount(); i++) {
                if (dbc->getUInt32(i, 0) == 1) {
                    playerFriendGroup = dbc->getUInt32(i, 4); // FriendGroup
                    // Also include our own factionGroup as friendly
                    playerFriendGroup |= dbc->getUInt32(i, 3);
                    break;
                }
            }
            // Find player's parent faction ID for individual enemy checks
            uint32_t playerFactionId = 0;
            for (uint32_t i = 0; i < dbc->getRecordCount(); i++) {
                if (dbc->getUInt32(i, 0) == 1) {
                    playerFactionId = dbc->getUInt32(i, 1); // Faction (parent)
                    break;
                }
            }
            // Second pass: classify each faction template
            for (uint32_t i = 0; i < dbc->getRecordCount(); i++) {
                uint32_t id = dbc->getUInt32(i, 0);
                uint32_t factionGroup = dbc->getUInt32(i, 3);
                uint32_t enemyGroup = dbc->getUInt32(i, 5);
                // Check group-level hostility
                bool hostile = (enemyGroup & playerFriendGroup) != 0;
                // Check if creature is a Monster type (factionGroup bit 8)
                // Bits: 1=Player, 2=Alliance, 4=Horde, 8=Monster
                if (!hostile && (factionGroup & 8) != 0) {
                    hostile = true;
                }
                // Check individual enemy faction IDs (fields 6-9)
                if (!hostile && playerFactionId > 0) {
                    for (int e = 6; e <= 9; e++) {
                        if (dbc->getUInt32(i, e) == playerFactionId) {
                            hostile = true;
                            break;
                        }
                    }
                }
                factionHostile[id] = hostile;
            }
            LOG_INFO("NpcManager: loaded ", dbc->getRecordCount(),
                     " faction templates (playerFriendGroup=0x", std::hex, playerFriendGroup, std::dec, ")");
        } else {
            LOG_WARNING("NpcManager: FactionTemplate.dbc not available, all NPCs default to hostile");
        }
    }

    // Spawn each NPC instance
    for (const auto* sPtr : active) {
        const auto& s = *sPtr;
        const std::string path = s.m2Path;

        auto it = loadedModels.find(path);
        if (it == loadedModels.end()) continue; // model failed to load

        uint32_t modelId = it->second;

        glm::vec3 canonical = toCanonicalSpawn(s, swapXY, globalRotDeg, pivotX, pivotY, globalDx, globalDy);
        glm::vec3 glPos = core::coords::canonicalToRender(canonical);
        // Keep authored indoor Z for named NPCs; terrain snap is mainly for critters/outdoor fauna.
        if (terrainManager && s.isCritter) {
            if (auto h = terrainManager->getHeightAt(glPos.x, glPos.y)) {
                glPos.z = *h + 0.05f;
            }
        }

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
        if (s.entry != 0) {
            unit->setEntry(s.entry);
        }
        unit->setNpcFlags(s.npcFlags);
        unit->setFactionTemplate(s.faction);

        // Determine hostility from faction template
        auto fIt = factionHostile.find(s.faction);
        unit->setHostile(fIt != factionHostile.end() ? fIt->second : false);

        // Store canonical WoW coordinates for targeting/server compatibility
        glm::vec3 spawnCanonical = core::coords::renderToCanonical(glPos);
        unit->setPosition(spawnCanonical.x, spawnCanonical.y, spawnCanonical.z, s.rotation);

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

uint32_t NpcManager::findRenderInstanceId(uint64_t guid) const {
    for (const auto& npc : npcs) {
        if (npc.guid == guid) return npc.renderInstanceId;
    }
    return 0;
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
