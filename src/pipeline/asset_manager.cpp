#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include "core/memory_monitor.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

#include "stb_image.h"

namespace wowee {
namespace pipeline {

namespace {
size_t parseEnvSizeMB(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return 0;
    }
    char* end = nullptr;
    unsigned long long mb = std::strtoull(v, &end, 10);
    if (end == v || mb == 0) {
        return 0;
    }
    if (mb > (std::numeric_limits<size_t>::max() / (1024ull * 1024ull))) {
        return 0;
    }
    return static_cast<size_t>(mb);
}
} // namespace

AssetManager::AssetManager() = default;
AssetManager::~AssetManager() {
    shutdown();
}

bool AssetManager::initialize(const std::string& dataPath_) {
    if (initialized) {
        LOG_WARNING("AssetManager already initialized");
        return true;
    }

    dataPath = dataPath_;
    LOG_INFO("Initializing asset manager with data path: ", dataPath);

    setupFileCacheBudget();

    std::string manifestPath = dataPath + "/manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
        LOG_ERROR("manifest.json not found in: ", dataPath);
        LOG_ERROR("Run asset_extract to extract MPQ archives first");
        return false;
    }

    if (!manifest_.load(manifestPath)) {
        LOG_ERROR("Failed to load manifest");
        return false;
    }

    initialized = true;
    LOG_INFO("Asset manager initialized: ", manifest_.getEntryCount(),
             " files indexed (file cache: ", fileCacheBudget / (1024 * 1024), " MB)");
    return true;
}

void AssetManager::setupFileCacheBudget() {
    auto& memMonitor = core::MemoryMonitor::getInstance();
    size_t recommendedBudget = memMonitor.getRecommendedCacheBudget();
    size_t dynamicBudget = (recommendedBudget * 3) / 4;

    const size_t envFixedMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MB");
    const size_t envMaxMB = parseEnvSizeMB("WOWEE_FILE_CACHE_MAX_MB");

    const size_t minBudgetBytes = 256ull * 1024ull * 1024ull;
    const size_t defaultMaxBudgetBytes = 32768ull * 1024ull * 1024ull;
    const size_t maxBudgetBytes = (envMaxMB > 0)
        ? (envMaxMB * 1024ull * 1024ull)
        : defaultMaxBudgetBytes;

    if (envFixedMB > 0) {
        fileCacheBudget = envFixedMB * 1024ull * 1024ull;
        if (fileCacheBudget < minBudgetBytes) {
            fileCacheBudget = minBudgetBytes;
        }
        LOG_WARNING("Asset file cache fixed via WOWEE_FILE_CACHE_MB=", envFixedMB,
                    " (effective ", fileCacheBudget / (1024 * 1024), " MB)");
    } else {
        fileCacheBudget = std::clamp(dynamicBudget, minBudgetBytes, maxBudgetBytes);
    }
}

void AssetManager::shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down asset manager");

    if (fileCacheHits + fileCacheMisses > 0) {
        float hitRate = (float)fileCacheHits / (fileCacheHits + fileCacheMisses) * 100.0f;
        LOG_INFO("File cache stats: ", fileCacheHits, " hits, ", fileCacheMisses, " misses (",
                 (int)hitRate, "% hit rate), ", fileCacheTotalBytes / 1024 / 1024, " MB cached");
    }

    clearCache();
    overlayLayers_.clear();
    initialized = false;
}

bool AssetManager::addOverlayManifest(const std::string& manifestPath, int priority, const std::string& id) {
    // Check for duplicate
    for (const auto& layer : overlayLayers_) {
        if (layer.id == id) {
            LOG_WARNING("Overlay '", id, "' already loaded, skipping");
            return false;
        }
    }

    ManifestLayer layer;
    layer.priority = priority;
    layer.id = id;

    if (!layer.manifest.load(manifestPath)) {
        LOG_ERROR("Failed to load overlay manifest: ", manifestPath);
        return false;
    }

    overlayLayers_.push_back(std::move(layer));

    // Sort by priority descending (highest priority first)
    std::sort(overlayLayers_.begin(), overlayLayers_.end(),
              [](const ManifestLayer& a, const ManifestLayer& b) {
                  return a.priority > b.priority;
              });

    LOG_INFO("Added overlay '", id, "' (priority ", priority, ", ",
             overlayLayers_.back().manifest.getEntryCount(), " files) from ", manifestPath);
    return true;
}

void AssetManager::removeOverlay(const std::string& id) {
    auto it = std::remove_if(overlayLayers_.begin(), overlayLayers_.end(),
                             [&id](const ManifestLayer& layer) { return layer.id == id; });
    if (it != overlayLayers_.end()) {
        overlayLayers_.erase(it, overlayLayers_.end());
        // Clear file cache since overlay removal changes file resolution
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            fileCache.clear();
            fileCacheTotalBytes = 0;
        }
        LOG_INFO("Removed overlay '", id, "', file cache cleared");
    }
}

std::vector<std::string> AssetManager::getOverlayIds() const {
    std::vector<std::string> ids;
    ids.reserve(overlayLayers_.size());
    for (const auto& layer : overlayLayers_) {
        ids.push_back(layer.id);
    }
    return ids;
}

std::string AssetManager::resolveLayeredPath(const std::string& normalizedPath) const {
    // For character textures, pick the highest-resolution variant across overlays/base.
    // Turtle/classic overlays often include low-res character skins; the base dataset may have higher-res.
    auto isCharacterTexture = [&]() -> bool {
        if (normalizedPath.size() < 4) return false;
        if (normalizedPath.rfind("character\\", 0) != 0) return false;
        return normalizedPath.compare(normalizedPath.size() - 4, 4, ".blp") == 0;
    };

    if (isCharacterTexture()) {
        uint64_t bestSize = 0;
        std::string bestFsPath;

        auto consider = [&](const AssetManifest& m) {
            if (const auto* e = m.lookup(normalizedPath)) {
                if (e->size > bestSize) {
                    bestSize = e->size;
                    bestFsPath = m.getBasePath() + "/" + e->filesystemPath;
                }
            }
        };

        for (const auto& layer : overlayLayers_) consider(layer.manifest);
        consider(manifest_);

        if (!bestFsPath.empty()) return bestFsPath;
    }

    // Default: check overlay manifests first (sorted by priority desc)
    for (const auto& layer : overlayLayers_) {
        std::string fsPath = layer.manifest.resolveFilesystemPath(normalizedPath);
        if (!fsPath.empty()) {
            return fsPath;
        }
    }
    // Fall back to base manifest
    return manifest_.resolveFilesystemPath(normalizedPath);
}

BLPImage AssetManager::loadTexture(const std::string& path) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return BLPImage();
    }

    std::string normalizedPath = normalizePath(path);

    LOG_DEBUG("Loading texture: ", normalizedPath);

    // Check for PNG override
    BLPImage pngImage = tryLoadPngOverride(normalizedPath);
    if (pngImage.isValid()) {
        return pngImage;
    }

    std::vector<uint8_t> blpData = readFile(normalizedPath);
    if (blpData.empty()) {
        LOG_WARNING("Texture not found: ", normalizedPath);
        return BLPImage();
    }

    BLPImage image = BLPLoader::load(blpData);
    if (!image.isValid()) {
        LOG_ERROR("Failed to load texture: ", normalizedPath);
        return BLPImage();
    }

    LOG_DEBUG("Loaded texture: ", normalizedPath, " (", image.width, "x", image.height, ")");
    return image;
}

BLPImage AssetManager::tryLoadPngOverride(const std::string& normalizedPath) const {
    if (normalizedPath.size() < 4) return BLPImage();

    std::string ext = normalizedPath.substr(normalizedPath.size() - 4);
    if (ext != ".blp") return BLPImage();

    std::string fsPath = resolveLayeredPath(normalizedPath);
    if (fsPath.empty()) return BLPImage();

    // Replace .blp/.BLP extension with .png
    std::string pngPath = fsPath.substr(0, fsPath.size() - 4) + ".png";
    if (!LooseFileReader::fileExists(pngPath)) {
        return BLPImage();
    }

    int w, h, channels;
    unsigned char* pixels = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        LOG_WARNING("PNG override exists but failed to load: ", pngPath);
        return BLPImage();
    }

    BLPImage image;
    image.width = w;
    image.height = h;
    image.channels = 4;
    image.format = BLPFormat::BLP2;
    image.compression = BLPCompression::ARGB8888;
    image.data.assign(pixels, pixels + (w * h * 4));
    stbi_image_free(pixels);

    LOG_INFO("PNG override loaded: ", pngPath, " (", w, "x", h, ")");
    return image;
}

void AssetManager::setExpansionDataPath(const std::string& path) {
    expansionDataPath_ = path;
    LOG_INFO("Expansion data path for CSV DBCs: ", expansionDataPath_);
}

std::shared_ptr<DBCFile> AssetManager::loadDBC(const std::string& name) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return nullptr;
    }

    auto it = dbcCache.find(name);
    if (it != dbcCache.end()) {
        LOG_DEBUG("DBC already loaded (cached): ", name);
        return it->second;
    }

    LOG_DEBUG("Loading DBC: ", name);

    std::vector<uint8_t> dbcData;

    // Try expansion-specific CSV first (e.g. Data/expansions/wotlk/db/Spell.csv)
    bool loadedFromCSV = false;
    if (!expansionDataPath_.empty()) {
        // Derive CSV name from DBC name: "Spell.dbc" -> "Spell.csv"
        std::string baseName = name;
        auto dot = baseName.rfind('.');
        if (dot != std::string::npos) {
            baseName = baseName.substr(0, dot);
        }
        std::string csvPath = expansionDataPath_ + "/db/" + baseName + ".csv";
        if (std::filesystem::exists(csvPath)) {
            std::ifstream f(csvPath, std::ios::binary | std::ios::ate);
            if (f) {
                auto size = f.tellg();
                if (size > 0) {
                    f.seekg(0);
                    dbcData.resize(static_cast<size_t>(size));
                    f.read(reinterpret_cast<char*>(dbcData.data()), size);
                    LOG_DEBUG("Found CSV DBC: ", csvPath);
                    loadedFromCSV = true;
                }
            }
        }
    }

    // Fall back to manifest (binary DBC from extracted MPQs)
    if (dbcData.empty()) {
        std::string dbcPath = "DBFilesClient\\" + name;
        dbcData = readFile(dbcPath);
        if (dbcData.empty()) {
            LOG_WARNING("DBC not found: ", name);
            return nullptr;
        }
    }

    auto dbc = std::make_shared<DBCFile>();
    if (!dbc->load(dbcData)) {
        // If CSV failed to parse, try binary DBC fallback
        if (loadedFromCSV) {
            LOG_WARNING("CSV DBC failed to parse: ", name, " — trying binary DBC fallback");
            dbcData.clear();
            std::string dbcPath = "DBFilesClient\\" + name;
            dbcData = readFile(dbcPath);
            if (!dbcData.empty()) {
                dbc = std::make_shared<DBCFile>();
                if (dbc->load(dbcData)) {
                    loadedFromCSV = false;
                    LOG_INFO("Binary DBC fallback succeeded: ", name);
                } else {
                    LOG_ERROR("Failed to load DBC: ", name);
                    return nullptr;
                }
            } else {
                LOG_ERROR("Failed to load DBC: ", name);
                return nullptr;
            }
        } else {
            LOG_ERROR("Failed to load DBC: ", name);
            return nullptr;
        }
    }

    // Validate CSV-loaded DBCs: if >50 records but all have ID 0 in field 0,
    // the CSV data is garbled (e.g. string data in the ID column).
    if (loadedFromCSV && dbc->getRecordCount() > 50) {
        uint32_t zeroIds = 0;
        const uint32_t sampleSize = std::min(dbc->getRecordCount(), 100u);
        for (uint32_t i = 0; i < sampleSize; ++i) {
            if (dbc->getUInt32(i, 0) == 0) ++zeroIds;
        }
        // If >80% of sampled records have ID 0, the CSV is garbled
        if (zeroIds > sampleSize * 4 / 5) {
            LOG_WARNING("CSV DBC '", name, "' has garbled field 0 (",
                        zeroIds, "/", sampleSize, " records with ID=0) — falling back to binary DBC");
            dbcData.clear();
            std::string dbcPath = "DBFilesClient\\" + name;
            dbcData = readFile(dbcPath);
            if (!dbcData.empty()) {
                dbc = std::make_shared<DBCFile>();
                if (!dbc->load(dbcData)) {
                    LOG_ERROR("Binary DBC fallback also failed: ", name);
                    return nullptr;
                }
                LOG_INFO("Binary DBC fallback succeeded: ", name);
            } else {
                LOG_WARNING("No binary DBC fallback available for: ", name);
            }
        }
    }

    dbcCache[name] = dbc;

    LOG_INFO("Loaded DBC: ", name, " (", dbc->getRecordCount(), " records)");
    return dbc;
}

std::shared_ptr<DBCFile> AssetManager::getDBC(const std::string& name) const {
    auto it = dbcCache.find(name);
    if (it != dbcCache.end()) {
        return it->second;
    }
    return nullptr;
}

bool AssetManager::fileExists(const std::string& path) const {
    if (!initialized) {
        return false;
    }
    std::string normalized = normalizePath(path);
    // Check overlay manifests first
    for (const auto& layer : overlayLayers_) {
        if (layer.manifest.hasEntry(normalized)) {
            return true;
        }
    }
    return manifest_.hasEntry(normalized);
}

std::vector<uint8_t> AssetManager::readFile(const std::string& path) const {
    if (!initialized) {
        return {};
    }

    std::string normalized = normalizePath(path);

    // Check cache first
    {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        auto it = fileCache.find(normalized);
        if (it != fileCache.end()) {
            it->second.lastAccessTime = ++fileCacheAccessCounter;
            fileCacheHits++;
            return it->second.data;
        }
    }

    // Read from filesystem using layered resolution (overlays first, then base)
    std::string fsPath = resolveLayeredPath(normalized);
    if (fsPath.empty()) {
        return {};
    }

    auto data = LooseFileReader::readFile(fsPath);
    if (data.empty()) {
        LOG_WARNING("Manifest entry exists but file unreadable: ", fsPath);
        return data;
    }

    // Add to cache if within budget
    size_t fileSize = data.size();
    if (fileSize > 0 && fileSize < fileCacheBudget / 2) {
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        // Evict old entries if needed (LRU)
        while (fileCacheTotalBytes + fileSize > fileCacheBudget && !fileCache.empty()) {
            auto lru = fileCache.begin();
            for (auto it = fileCache.begin(); it != fileCache.end(); ++it) {
                if (it->second.lastAccessTime < lru->second.lastAccessTime) {
                    lru = it;
                }
            }
            fileCacheTotalBytes -= lru->second.data.size();
            fileCache.erase(lru);
        }

        CachedFile cached;
        cached.data = data;
        cached.lastAccessTime = ++fileCacheAccessCounter;
        fileCache[normalized] = std::move(cached);
        fileCacheTotalBytes += fileSize;
    }

    return data;
}

std::vector<uint8_t> AssetManager::readFileOptional(const std::string& path) const {
    if (!initialized) {
        return {};
    }
    if (!fileExists(path)) {
        return {};
    }
    return readFile(path);
}

void AssetManager::clearDBCCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    dbcCache.clear();
    LOG_INFO("Cleared DBC cache");
}

void AssetManager::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    dbcCache.clear();
    fileCache.clear();
    fileCacheTotalBytes = 0;
    fileCacheAccessCounter = 0;
    LOG_INFO("Cleared asset cache (DBC + file cache)");
}

std::string AssetManager::normalizePath(const std::string& path) const {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

} // namespace pipeline
} // namespace wowee
