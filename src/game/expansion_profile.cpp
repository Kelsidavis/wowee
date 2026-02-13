#include "game/expansion_profile.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

// Minimal JSON parsing (no external dependency) — expansion.json is tiny and flat.
// We parse the subset we need: strings, integers, arrays of integers.
namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n\"");
    size_t end = s.find_last_not_of(" \t\r\n\",");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Quick-and-dirty JSON value extractor for flat objects.
// Returns the raw value string for a given key, or empty.
std::string jsonValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
        ++pos;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        size_t end = json.find('"', pos + 1);
        return (end != std::string::npos) ? json.substr(pos + 1, end - pos - 1) : "";
    }
    if (json[pos] == '{') {
        // Nested object — return content between braces
        size_t depth = 1;
        size_t start = pos + 1;
        for (size_t i = start; i < json.size() && depth > 0; ++i) {
            if (json[i] == '{') ++depth;
            else if (json[i] == '}') { --depth; if (depth == 0) return json.substr(start, i - start); }
        }
        return "";
    }
    if (json[pos] == '[') {
        // Array — return content between brackets (including brackets)
        size_t end = json.find(']', pos);
        return (end != std::string::npos) ? json.substr(pos, end - pos + 1) : "";
    }
    // Number or other literal
    size_t end = json.find_first_of(",}\n\r", pos);
    return trim(json.substr(pos, end - pos));
}

int jsonInt(const std::string& json, const std::string& key, int def = 0) {
    std::string v = jsonValue(json, key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

std::vector<uint32_t> jsonUintArray(const std::string& json, const std::string& key) {
    std::vector<uint32_t> result;
    std::string arr = jsonValue(json, key);
    if (arr.empty() || arr.front() != '[') return result;
    // Strip brackets
    arr = arr.substr(1, arr.size() - 2);
    std::istringstream ss(arr);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        std::string t = trim(tok);
        if (!t.empty()) {
            try { result.push_back(static_cast<uint32_t>(std::stoul(t))); } catch (...) {}
        }
    }
    return result;
}

} // namespace

namespace wowee {
namespace game {

std::string ExpansionProfile::versionString() const {
    std::ostringstream ss;
    ss << (int)majorVersion << "." << (int)minorVersion << "." << (int)patchVersion;
    // Append letter suffix for known builds
    if (majorVersion == 3 && minorVersion == 3 && patchVersion == 5) ss << "a";
    else if (majorVersion == 2 && minorVersion == 4 && patchVersion == 3) ss << "";
    else if (majorVersion == 1 && minorVersion == 12 && patchVersion == 1) ss << "";
    return ss.str();
}

size_t ExpansionRegistry::initialize(const std::string& dataRoot) {
    profiles_.clear();
    activeId_.clear();

    std::string expansionsDir = dataRoot + "/expansions";
    std::error_code ec;
    if (!std::filesystem::is_directory(expansionsDir, ec)) {
        LOG_WARNING("ExpansionRegistry: no expansions/ directory at ", expansionsDir);
        return 0;
    }

    for (auto& entry : std::filesystem::directory_iterator(expansionsDir, ec)) {
        if (!entry.is_directory()) continue;
        std::string jsonPath = entry.path().string() + "/expansion.json";
        if (std::filesystem::exists(jsonPath, ec)) {
            loadProfile(jsonPath, entry.path().string());
        }
    }

    // Sort by build number (ascending: classic < tbc < wotlk < cata)
    std::sort(profiles_.begin(), profiles_.end(),
              [](const ExpansionProfile& a, const ExpansionProfile& b) { return a.build < b.build; });

    // Default to WotLK if available, otherwise the last (highest build)
    if (!profiles_.empty()) {
        auto it = std::find_if(profiles_.begin(), profiles_.end(),
                               [](const ExpansionProfile& p) { return p.id == "wotlk"; });
        activeId_ = (it != profiles_.end()) ? it->id : profiles_.back().id;
    }

    LOG_INFO("ExpansionRegistry: discovered ", profiles_.size(), " expansion(s), active=", activeId_);
    return profiles_.size();
}

const ExpansionProfile* ExpansionRegistry::getProfile(const std::string& id) const {
    for (auto& p : profiles_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

bool ExpansionRegistry::setActive(const std::string& id) {
    if (!getProfile(id)) return false;
    activeId_ = id;
    return true;
}

const ExpansionProfile* ExpansionRegistry::getActive() const {
    return getProfile(activeId_);
}

bool ExpansionRegistry::loadProfile(const std::string& jsonPath, const std::string& dirPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return false;

    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    ExpansionProfile p;
    p.id = jsonValue(json, "id");
    p.name = jsonValue(json, "name");
    p.shortName = jsonValue(json, "shortName");
    p.dataPath = dirPath;

    // Version nested object
    std::string ver = jsonValue(json, "version");
    if (!ver.empty()) {
        p.majorVersion = static_cast<uint8_t>(jsonInt(ver, "major"));
        p.minorVersion = static_cast<uint8_t>(jsonInt(ver, "minor"));
        p.patchVersion = static_cast<uint8_t>(jsonInt(ver, "patch"));
    }

    p.build = static_cast<uint16_t>(jsonInt(json, "build"));
    p.protocolVersion = static_cast<uint8_t>(jsonInt(json, "protocolVersion"));
    p.maxLevel = static_cast<uint32_t>(jsonInt(json, "maxLevel", 60));
    p.races = jsonUintArray(json, "races");
    p.classes = jsonUintArray(json, "classes");

    if (p.id.empty() || p.build == 0) {
        LOG_WARNING("ExpansionRegistry: skipping invalid profile at ", jsonPath);
        return false;
    }

    LOG_INFO("ExpansionRegistry: loaded '", p.name, "' (", p.shortName,
             ") v", p.versionString(), " build=", p.build);
    profiles_.push_back(std::move(p));
    return true;
}

} // namespace game
} // namespace wowee
