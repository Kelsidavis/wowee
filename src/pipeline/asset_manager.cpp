#include "pipeline/asset_manager.hpp"
#include "core/logger.hpp"
#include "core/memory_monitor.hpp"
#include <algorithm>

namespace wowee {
namespace pipeline {

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

    // Initialize MPQ manager
    if (!mpqManager.initialize(dataPath)) {
        LOG_ERROR("Failed to initialize MPQ manager");
        return false;
    }

    // Set dynamic file cache budget based on available RAM
    auto& memMonitor = core::MemoryMonitor::getInstance();
    size_t recommendedBudget = memMonitor.getRecommendedCacheBudget();
    fileCacheBudget = recommendedBudget / 2;  // Split budget: half for file cache, half for other caches

    initialized = true;
    LOG_INFO("Asset manager initialized (dynamic file cache: ",
             fileCacheBudget / (1024 * 1024), " MB, adjusts based on RAM)");
    return true;
}

void AssetManager::shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Shutting down asset manager");

    // Log cache statistics
    if (fileCacheHits + fileCacheMisses > 0) {
        float hitRate = (float)fileCacheHits / (fileCacheHits + fileCacheMisses) * 100.0f;
        LOG_INFO("File cache stats: ", fileCacheHits, " hits, ", fileCacheMisses, " misses (",
                 (int)hitRate, "% hit rate), ", fileCacheTotalBytes / 1024 / 1024, " MB cached");
    }

    clearCache();
    mpqManager.shutdown();

    initialized = false;
}

BLPImage AssetManager::loadTexture(const std::string& path) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return BLPImage();
    }

    // Normalize path
    std::string normalizedPath = normalizePath(path);

    LOG_DEBUG("Loading texture: ", normalizedPath);

    // Read BLP file from MPQ (must hold readMutex — StormLib is not thread-safe)
    std::vector<uint8_t> blpData;
    {
        std::lock_guard<std::mutex> lock(readMutex);
        blpData = mpqManager.readFile(normalizedPath);
    }
    if (blpData.empty()) {
        LOG_WARNING("Texture not found: ", normalizedPath);
        return BLPImage();
    }

    // Load BLP
    BLPImage image = BLPLoader::load(blpData);
    if (!image.isValid()) {
        LOG_ERROR("Failed to load texture: ", normalizedPath);
        return BLPImage();
    }

    LOG_INFO("Loaded texture: ", normalizedPath, " (", image.width, "x", image.height, ")");
    return image;
}

std::shared_ptr<DBCFile> AssetManager::loadDBC(const std::string& name) {
    if (!initialized) {
        LOG_ERROR("AssetManager not initialized");
        return nullptr;
    }

    // Check cache first
    auto it = dbcCache.find(name);
    if (it != dbcCache.end()) {
        LOG_DEBUG("DBC already loaded (cached): ", name);
        return it->second;
    }

    LOG_DEBUG("Loading DBC: ", name);

    // Construct DBC path (DBFilesClient directory)
    std::string dbcPath = "DBFilesClient\\" + name;

    // Read DBC file from MPQ (must hold readMutex — StormLib is not thread-safe)
    std::vector<uint8_t> dbcData;
    {
        std::lock_guard<std::mutex> lock(readMutex);
        dbcData = mpqManager.readFile(dbcPath);
    }
    if (dbcData.empty()) {
        LOG_WARNING("DBC not found: ", dbcPath);
        return nullptr;
    }

    // Load DBC
    auto dbc = std::make_shared<DBCFile>();
    if (!dbc->load(dbcData)) {
        LOG_ERROR("Failed to load DBC: ", dbcPath);
        return nullptr;
    }

    // Cache the DBC
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

    std::lock_guard<std::mutex> lock(readMutex);
    return mpqManager.fileExists(normalizePath(path));
}

std::vector<uint8_t> AssetManager::readFile(const std::string& path) const {
    if (!initialized) {
        return std::vector<uint8_t>();
    }

    std::string normalized = normalizePath(path);
    std::lock_guard<std::mutex> lock(readMutex);

    // Check cache first
    auto it = fileCache.find(normalized);
    if (it != fileCache.end()) {
        // Cache hit - update access time and return cached data
        it->second.lastAccessTime = ++fileCacheAccessCounter;
        fileCacheHits++;
        return it->second.data;
    }

    // Cache miss - decompress from MPQ
    fileCacheMisses++;
    std::vector<uint8_t> data = mpqManager.readFile(normalized);
    if (data.empty()) {
        return data;  // File not found
    }

    // Add to cache if within budget
    size_t fileSize = data.size();
    if (fileSize > 0 && fileSize < fileCacheBudget / 2) {  // Don't cache files > 50% of budget (very aggressive)
        // Evict old entries if needed (LRU)
        while (fileCacheTotalBytes + fileSize > fileCacheBudget && !fileCache.empty()) {
            // Find least recently used entry
            auto lru = fileCache.begin();
            for (auto it = fileCache.begin(); it != fileCache.end(); ++it) {
                if (it->second.lastAccessTime < lru->second.lastAccessTime) {
                    lru = it;
                }
            }
            fileCacheTotalBytes -= lru->second.data.size();
            fileCache.erase(lru);
        }

        // Add new entry
        CachedFile cached;
        cached.data = data;
        cached.lastAccessTime = ++fileCacheAccessCounter;
        fileCache[normalized] = std::move(cached);
        fileCacheTotalBytes += fileSize;
    }

    return data;
}

void AssetManager::clearCache() {
    std::lock_guard<std::mutex> lock(readMutex);
    dbcCache.clear();
    fileCache.clear();
    fileCacheTotalBytes = 0;
    fileCacheAccessCounter = 0;
    LOG_INFO("Cleared asset cache (DBC + file cache)");
}

std::string AssetManager::normalizePath(const std::string& path) const {
    std::string normalized = path;

    // Convert forward slashes to backslashes (WoW uses backslashes)
    std::replace(normalized.begin(), normalized.end(), '/', '\\');

    return normalized;
}

} // namespace pipeline
} // namespace wowee
