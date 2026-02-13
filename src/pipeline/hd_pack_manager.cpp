#include "pipeline/hd_pack_manager.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace wowee {
namespace pipeline {

namespace {
// Minimal JSON string value parser (key must be unique in the flat object)
std::string jsonStringValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

// Parse a JSON number value
uint32_t jsonUintValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    return static_cast<uint32_t>(std::strtoul(json.c_str() + pos, nullptr, 10));
}

// Parse a JSON string array value
std::vector<std::string> jsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return result;
    pos = json.find('[', pos + needle.size());
    if (pos == std::string::npos) return result;
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return result;
    std::string arr = json.substr(pos + 1, end - pos - 1);
    size_t p = 0;
    while (p < arr.size()) {
        size_t qs = arr.find('"', p);
        if (qs == std::string::npos) break;
        size_t qe = arr.find('"', qs + 1);
        if (qe == std::string::npos) break;
        result.push_back(arr.substr(qs + 1, qe - qs - 1));
        p = qe + 1;
    }
    return result;
}
} // namespace

void HDPackManager::initialize(const std::string& hdRootPath) {
    packs_.clear();

    if (!std::filesystem::exists(hdRootPath) || !std::filesystem::is_directory(hdRootPath)) {
        LOG_DEBUG("HD pack directory not found: ", hdRootPath);
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(hdRootPath)) {
        if (!entry.is_directory()) continue;

        std::string packJsonPath = entry.path().string() + "/pack.json";
        if (!std::filesystem::exists(packJsonPath)) continue;

        std::ifstream f(packJsonPath);
        if (!f.is_open()) continue;

        std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        HDPack pack;
        pack.id = jsonStringValue(json, "id");
        pack.name = jsonStringValue(json, "name");
        pack.group = jsonStringValue(json, "group");
        pack.totalSizeMB = jsonUintValue(json, "totalSizeMB");
        pack.expansions = jsonStringArray(json, "expansions");
        pack.packDir = entry.path().string();
        pack.manifestPath = entry.path().string() + "/manifest.json";

        if (pack.id.empty()) {
            LOG_WARNING("HD pack in ", entry.path().string(), " has no id, skipping");
            continue;
        }

        if (!std::filesystem::exists(pack.manifestPath)) {
            LOG_WARNING("HD pack '", pack.id, "' missing manifest.json, skipping");
            continue;
        }

        // Apply saved enabled state if available
        auto it = enabledState_.find(pack.id);
        if (it != enabledState_.end()) {
            pack.enabled = it->second;
        }

        LOG_INFO("Discovered HD pack: '", pack.id, "' (", pack.name, ") ",
                 pack.totalSizeMB, " MB, ", pack.expansions.size(), " expansions");
        packs_.push_back(std::move(pack));
    }

    LOG_INFO("HDPackManager: found ", packs_.size(), " packs in ", hdRootPath);
}

std::vector<const HDPack*> HDPackManager::getPacksForExpansion(const std::string& expansionId) const {
    std::vector<const HDPack*> result;
    for (const auto& pack : packs_) {
        if (pack.expansions.empty()) {
            // No expansion filter = compatible with all
            result.push_back(&pack);
        } else {
            for (const auto& exp : pack.expansions) {
                if (exp == expansionId) {
                    result.push_back(&pack);
                    break;
                }
            }
        }
    }
    return result;
}

void HDPackManager::setPackEnabled(const std::string& packId, bool enabled) {
    enabledState_[packId] = enabled;
    for (auto& pack : packs_) {
        if (pack.id == packId) {
            pack.enabled = enabled;
            break;
        }
    }
}

bool HDPackManager::isPackEnabled(const std::string& packId) const {
    auto it = enabledState_.find(packId);
    return it != enabledState_.end() && it->second;
}

void HDPackManager::applyToAssetManager(AssetManager* assetManager, const std::string& expansionId) {
    if (!assetManager) return;

    // Remove previously applied overlays
    for (const auto& overlayId : appliedOverlayIds_) {
        assetManager->removeOverlay(overlayId);
    }
    appliedOverlayIds_.clear();

    // Get packs compatible with current expansion
    auto compatiblePacks = getPacksForExpansion(expansionId);
    int priorityOffset = 0;

    for (const auto* pack : compatiblePacks) {
        if (!pack->enabled) continue;

        std::string overlayId = "hd_" + pack->id;
        int priority = HD_OVERLAY_PRIORITY_BASE + priorityOffset;

        if (assetManager->addOverlayManifest(pack->manifestPath, priority, overlayId)) {
            appliedOverlayIds_.push_back(overlayId);
            LOG_INFO("Applied HD pack '", pack->id, "' as overlay (priority ", priority, ")");
        }
        ++priorityOffset;
    }

    if (!appliedOverlayIds_.empty()) {
        LOG_INFO("Applied ", appliedOverlayIds_.size(), " HD pack overlays");
    }
}

void HDPackManager::saveSettings(const std::string& settingsPath) const {
    std::ofstream f(settingsPath, std::ios::app);
    if (!f.is_open()) return;

    for (const auto& [packId, enabled] : enabledState_) {
        f << "hd_pack_" << packId << "=" << (enabled ? "1" : "0") << "\n";
    }
}

void HDPackManager::loadSettings(const std::string& settingsPath) {
    std::ifstream f(settingsPath);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.substr(0, 8) != "hd_pack_") continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string packId = line.substr(8, eq - 8);
        bool enabled = (line.substr(eq + 1) == "1");
        enabledState_[packId] = enabled;
    }
}

} // namespace pipeline
} // namespace wowee
