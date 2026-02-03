#pragma once

#include "pipeline/adt_loader.hpp"
#include "pipeline/terrain_mesh.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/wmo_loader.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <glm/glm.hpp>

namespace wowee {

namespace pipeline { class AssetManager; }
namespace rendering { class TerrainRenderer; class Camera; class WaterRenderer; class M2Renderer; class WMORenderer; }

namespace rendering {

/**
 * Terrain tile coordinates
 */
struct TileCoord {
    int x;
    int y;

    bool operator==(const TileCoord& other) const {
        return x == other.x && y == other.y;
    }

    struct Hash {
        size_t operator()(const TileCoord& coord) const {
            return std::hash<int>()(coord.x) ^ (std::hash<int>()(coord.y) << 1);
        }
    };
};

/**
 * Loaded terrain tile data
 */
struct TerrainTile {
    TileCoord coord;
    pipeline::ADTTerrain terrain;
    pipeline::TerrainMesh mesh;
    bool loaded = false;

    // Tile bounds in world coordinates
    float minX, minY, maxX, maxY;

    // Instance IDs for cleanup on unload
    std::vector<uint32_t> wmoInstanceIds;
    std::vector<uint32_t> m2InstanceIds;
    std::vector<uint32_t> doodadUniqueIds;  // For dedup cleanup on unload
};

/**
 * Pre-processed tile data ready for GPU upload (produced by background thread)
 */
struct PendingTile {
    TileCoord coord;
    pipeline::ADTTerrain terrain;
    pipeline::TerrainMesh mesh;

    // Pre-loaded M2 data
    struct M2Ready {
        uint32_t modelId;
        pipeline::M2Model model;
        std::string path;
    };
    std::vector<M2Ready> m2Models;

    // M2 instance placement data (references modelId from m2Models)
    struct M2Placement {
        uint32_t modelId;
        uint32_t uniqueId;
        glm::vec3 position;
        glm::vec3 rotation;
        float scale;
    };
    std::vector<M2Placement> m2Placements;

    // Pre-loaded WMO data
    struct WMOReady {
        uint32_t modelId;
        pipeline::WMOModel model;
        glm::vec3 position;
        glm::vec3 rotation;
    };
    std::vector<WMOReady> wmoModels;

    // WMO doodad M2 models (M2s placed inside WMOs)
    struct WMODoodadReady {
        uint32_t modelId;
        pipeline::M2Model model;
        glm::vec3 worldPosition;   // For frustum culling
        glm::mat4 modelMatrix;     // Pre-computed world transform
    };
    std::vector<WMODoodadReady> wmoDoodads;
};

/**
 * Terrain manager for multi-tile terrain streaming
 *
 * Handles loading and unloading terrain tiles based on camera position
 */
class TerrainManager {
public:
    TerrainManager();
    ~TerrainManager();

    /**
     * Initialize terrain manager
     * @param assetManager Asset manager for loading files
     * @param terrainRenderer Terrain renderer for GPU upload
     */
    bool initialize(pipeline::AssetManager* assetManager, TerrainRenderer* terrainRenderer);

    /**
     * Update terrain streaming based on camera position
     * @param camera Current camera
     * @param deltaTime Time since last update
     */
    void update(const Camera& camera, float deltaTime);

    /**
     * Set map name
     * @param mapName Map name (e.g., "Azeroth", "Kalimdor")
     */
    void setMapName(const std::string& mapName) { this->mapName = mapName; }

    /**
     * Load a single tile
     * @param x Tile X coordinate (0-63)
     * @param y Tile Y coordinate (0-63)
     * @return true if loaded successfully
     */
    bool loadTile(int x, int y);

    /**
     * Unload a tile
     * @param x Tile X coordinate
     * @param y Tile Y coordinate
     */
    void unloadTile(int x, int y);

    /**
     * Unload all tiles
     */
    void unloadAll();

    /**
     * Set streaming parameters
     */
    void setLoadRadius(int radius) { loadRadius = radius; }
    void setUnloadRadius(int radius) { unloadRadius = radius; }
    void setStreamingEnabled(bool enabled) { streamingEnabled = enabled; }
    void setWaterRenderer(WaterRenderer* renderer) { waterRenderer = renderer; }
    void setM2Renderer(M2Renderer* renderer) { m2Renderer = renderer; }
    void setWMORenderer(WMORenderer* renderer) { wmoRenderer = renderer; }

    /**
     * Get terrain height at GL coordinates
     * @param glX GL X position
     * @param glY GL Y position
     * @return Height (GL Z) if terrain loaded at that position, empty otherwise
     */
    std::optional<float> getHeightAt(float glX, float glY) const;

    /**
     * Get statistics
     */
    int getLoadedTileCount() const { return static_cast<int>(loadedTiles.size()); }
    TileCoord getCurrentTile() const { return currentTile; }

private:
    /**
     * Get tile coordinates from world position
     */
    TileCoord worldToTile(float worldX, float worldY) const;

    /**
     * Get world bounds for a tile
     */
    void getTileBounds(const TileCoord& coord, float& minX, float& minY,
                       float& maxX, float& maxY) const;

    /**
     * Build ADT file path
     */
    std::string getADTPath(const TileCoord& coord) const;

    /**
     * Load tiles in radius around current tile
     */
    void streamTiles();

    /**
     * Background thread: prepare tile data (CPU work only, no OpenGL)
     */
    std::unique_ptr<PendingTile> prepareTile(int x, int y);

    /**
     * Main thread: upload prepared tile data to GPU
     */
    void finalizeTile(std::unique_ptr<PendingTile> pending);

    /**
     * Background worker thread loop
     */
    void workerLoop();

    /**
     * Main thread: poll for completed tiles and upload to GPU
     */
    void processReadyTiles();

    pipeline::AssetManager* assetManager = nullptr;
    TerrainRenderer* terrainRenderer = nullptr;
    WaterRenderer* waterRenderer = nullptr;
    M2Renderer* m2Renderer = nullptr;
    WMORenderer* wmoRenderer = nullptr;

    std::string mapName = "Azeroth";

    // Loaded tiles (keyed by coordinate)
    std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoord::Hash> loadedTiles;

    // Tiles that failed to load (don't retry)
    std::unordered_map<TileCoord, bool, TileCoord::Hash> failedTiles;

    // Current tile (where camera is)
    TileCoord currentTile = {-1, -1};
    TileCoord lastStreamTile = {-1, -1};

    // Streaming parameters
    bool streamingEnabled = true;
    int loadRadius = 6;      // Load tiles within this radius (13x13 grid, ~3200 units)
    int unloadRadius = 8;    // Unload tiles beyond this radius (~4266 units)
    float updateInterval = 0.1f;  // Check streaming every 0.1 seconds
    float timeSinceLastUpdate = 0.0f;

    // Tile size constants (WoW ADT specifications)
    // A tile (ADT) = 16x16 chunks = 533.33 units across
    // A chunk = 8x8 vertex quads = 33.33 units across
    static constexpr float TILE_SIZE = 533.33333f;          // One tile = 533.33 units
    static constexpr float CHUNK_SIZE = 33.33333f;          // One chunk = 33.33 units

    // Background loading thread
    std::thread workerThread;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::queue<TileCoord> loadQueue;
    std::queue<std::unique_ptr<PendingTile>> readyQueue;
    std::atomic<bool> workerRunning{false};

    // Track tiles currently queued or being processed to avoid duplicates
    std::unordered_map<TileCoord, bool, TileCoord::Hash> pendingTiles;

    // Dedup set for doodad placements across tile boundaries
    std::unordered_set<uint32_t> placedDoodadIds;
};

} // namespace rendering
} // namespace wowee
