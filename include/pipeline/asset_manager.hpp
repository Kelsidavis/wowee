#pragma once

#include "pipeline/mpq_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include <memory>
#include <string>
#include <map>
#include <mutex>

namespace wowee {
namespace pipeline {

/**
 * AssetManager - Unified interface for loading WoW assets
 *
 * Coordinates MPQ archives, texture loading, and database files
 */
class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    /**
     * Initialize asset manager
     * @param dataPath Path to WoW Data directory
     * @return true if initialization succeeded
     */
    bool initialize(const std::string& dataPath);

    /**
     * Shutdown and cleanup
     */
    void shutdown();

    /**
     * Check if asset manager is initialized
     */
    bool isInitialized() const { return initialized; }

    /**
     * Load a BLP texture
     * @param path Virtual path to BLP file (e.g., "Textures\\Minimap\\Background.blp")
     * @return BLP image (check isValid())
     */
    BLPImage loadTexture(const std::string& path);

    /**
     * Load a DBC file
     * @param name DBC file name (e.g., "Map.dbc")
     * @return Loaded DBC file (check isLoaded())
     */
    std::shared_ptr<DBCFile> loadDBC(const std::string& name);

    /**
     * Get a cached DBC file
     * @param name DBC file name
     * @return Cached DBC or nullptr if not loaded
     */
    std::shared_ptr<DBCFile> getDBC(const std::string& name) const;

    /**
     * Check if a file exists in MPQ archives
     * @param path Virtual file path
     * @return true if file exists
     */
    bool fileExists(const std::string& path) const;

    /**
     * Read raw file data from MPQ archives
     * @param path Virtual file path
     * @return File contents (empty if not found)
     */
    std::vector<uint8_t> readFile(const std::string& path) const;

    /**
     * Get MPQ manager for direct access
     */
    MPQManager& getMPQManager() { return mpqManager; }
    const MPQManager& getMPQManager() const { return mpqManager; }

    /**
     * Get loaded DBC count
     */
    size_t getLoadedDBCCount() const { return dbcCache.size(); }

    /**
     * Get file cache stats
     */
    size_t getFileCacheSize() const { return fileCacheTotalBytes; }
    size_t getFileCacheHits() const { return fileCacheHits; }
    size_t getFileCacheMisses() const { return fileCacheMisses; }

    /**
     * Clear all cached resources
     */
    void clearCache();

private:
    bool initialized = false;
    std::string dataPath;

    MPQManager mpqManager;
    mutable std::mutex readMutex;
    std::map<std::string, std::shared_ptr<DBCFile>> dbcCache;

    // Decompressed file cache (LRU, dynamic budget based on system RAM)
    struct CachedFile {
        std::vector<uint8_t> data;
        uint64_t lastAccessTime;
    };
    mutable std::map<std::string, CachedFile> fileCache;
    mutable size_t fileCacheTotalBytes = 0;
    mutable uint64_t fileCacheAccessCounter = 0;
    mutable size_t fileCacheHits = 0;
    mutable size_t fileCacheMisses = 0;
    mutable size_t fileCacheBudget = 1024 * 1024 * 1024;  // Dynamic, starts at 1GB

    /**
     * Normalize path for case-insensitive lookup
     */
    std::string normalizePath(const std::string& path) const;
};

} // namespace pipeline
} // namespace wowee
