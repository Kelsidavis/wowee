#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>

namespace wowee {
namespace pipeline {
    struct WMOModel;
    struct WMOGroup;
    class AssetManager;
}

namespace rendering {

class Camera;
class Shader;

/**
 * WMO (World Model Object) Renderer
 *
 * Renders buildings, dungeons, and large structures from WMO files.
 * Features:
 * - Multi-material rendering
 * - Batched rendering per group
 * - Frustum culling
 * - Portal visibility (future)
 * - Dynamic lighting support (future)
 */
class WMORenderer {
public:
    WMORenderer();
    ~WMORenderer();

    /**
     * Initialize renderer and create shaders
     * @param assetManager Asset manager for loading textures (optional)
     */
    bool initialize(pipeline::AssetManager* assetManager = nullptr);

    /**
     * Cleanup GPU resources
     */
    void shutdown();

    /**
     * Load WMO model and create GPU resources
     * @param model WMO model with geometry data
     * @param id Unique identifier for this WMO instance
     * @return True if successful
     */
    bool loadModel(const pipeline::WMOModel& model, uint32_t id);

    /**
     * Unload WMO model and free GPU resources
     * @param id WMO model identifier
     */
    void unloadModel(uint32_t id);

    /**
     * Create a WMO instance in the world
     * @param modelId WMO model to instantiate
     * @param position World position
     * @param rotation Rotation (euler angles in radians)
     * @param scale Uniform scale
     * @return Instance ID
     */
    uint32_t createInstance(uint32_t modelId, const glm::vec3& position,
                           const glm::vec3& rotation = glm::vec3(0.0f),
                           float scale = 1.0f);

    /**
     * Remove WMO instance
     * @param instanceId Instance to remove
     */
    void removeInstance(uint32_t instanceId);

    /**
     * Remove all instances
     */
    void clearInstances();

    /**
     * Render all WMO instances
     * @param camera Camera for view/projection matrices
     * @param view View matrix
     * @param projection Projection matrix
     */
    void render(const Camera& camera, const glm::mat4& view, const glm::mat4& projection);

    /**
     * Get number of loaded models
     */
    uint32_t getModelCount() const { return loadedModels.size(); }

    /**
     * Get number of active instances
     */
    uint32_t getInstanceCount() const { return instances.size(); }

    /**
     * Remove models that have no instances referencing them
     * Call periodically to free GPU memory
     */
    void cleanupUnusedModels();

    /**
     * Get total triangle count (all instances)
     */
    uint32_t getTotalTriangleCount() const;

    /**
     * Get total draw call count (last frame)
     */
    uint32_t getDrawCallCount() const { return lastDrawCalls; }

    /**
     * Enable/disable wireframe rendering
     */
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }

    /**
     * Enable/disable frustum culling
     */
    void setFrustumCulling(bool enabled) { frustumCulling = enabled; }

    /**
     * Get floor height at a GL position via ray-triangle intersection
     */
    std::optional<float> getFloorHeight(float glX, float glY, float glZ) const;

    /**
     * Check wall collision and adjust position
     * @param from Starting position
     * @param to Desired position
     * @param adjustedPos Output adjusted position (pushed away from walls)
     * @return true if collision occurred
     */
    bool checkWallCollision(const glm::vec3& from, const glm::vec3& to, glm::vec3& adjustedPos) const;

    /**
     * Check if a position is inside any WMO
     * @param outModelId If not null, receives the model ID of the WMO
     * @return true if inside a WMO
     */
    bool isInsideWMO(float glX, float glY, float glZ, uint32_t* outModelId = nullptr) const;

    /**
     * Raycast against WMO bounding boxes for camera collision
     * @param origin Ray origin (e.g., character head position)
     * @param direction Ray direction (normalized)
     * @param maxDistance Maximum ray distance to check
     * @return Distance to first intersection, or maxDistance if no hit
     */
    float raycastBoundingBoxes(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;

private:
    /**
     * WMO group GPU resources
     */
    struct GroupResources {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        glm::vec3 boundingBoxMin;
        glm::vec3 boundingBoxMax;

        // Material batches (start index, count, material ID)
        struct Batch {
            uint32_t startIndex;   // First index in EBO
            uint32_t indexCount;   // Number of indices to draw
            uint8_t materialId;    // Material/texture reference
        };
        std::vector<Batch> batches;

        // Collision geometry (positions only, for floor raycasting)
        std::vector<glm::vec3> collisionVertices;
        std::vector<uint16_t> collisionIndices;
    };

    /**
     * Loaded WMO model data
     */
    struct ModelData {
        uint32_t id;
        std::vector<GroupResources> groups;
        glm::vec3 boundingBoxMin;
        glm::vec3 boundingBoxMax;

        // Texture handles for this model (indexed by texture path order)
        std::vector<GLuint> textures;

        // Material texture indices (materialId -> texture index)
        std::vector<uint32_t> materialTextureIndices;

        // Material blend modes (materialId -> blendMode; 1 = alpha-test cutout)
        std::vector<uint32_t> materialBlendModes;

        uint32_t getTotalTriangles() const {
            uint32_t total = 0;
            for (const auto& group : groups) {
                total += group.indexCount / 3;
            }
            return total;
        }
    };

    /**
     * WMO instance in the world
     */
    struct WMOInstance {
        uint32_t id;
        uint32_t modelId;
        glm::vec3 position;
        glm::vec3 rotation;  // Euler angles (radians)
        float scale;
        glm::mat4 modelMatrix;

        void updateModelMatrix();
    };

    /**
     * Create GPU resources for a WMO group
     */
    bool createGroupResources(const pipeline::WMOGroup& group, GroupResources& resources);

    /**
     * Render a single group
     */
    void renderGroup(const GroupResources& group, const ModelData& model,
                    const glm::mat4& modelMatrix,
                    const glm::mat4& view, const glm::mat4& projection);

    /**
     * Check if group is visible in frustum
     */
    bool isGroupVisible(const GroupResources& group, const glm::mat4& modelMatrix,
                       const Camera& camera) const;

    /**
     * Load a texture from path
     */
    GLuint loadTexture(const std::string& path);

    // Shader
    std::unique_ptr<Shader> shader;

    // Asset manager for loading textures
    pipeline::AssetManager* assetManager = nullptr;

    // Texture cache (path -> texture ID)
    std::unordered_map<std::string, GLuint> textureCache;

    // Default white texture
    GLuint whiteTexture = 0;

    // Loaded models (modelId -> ModelData)
    std::unordered_map<uint32_t, ModelData> loadedModels;

    // Active instances
    std::vector<WMOInstance> instances;
    uint32_t nextInstanceId = 1;

    // Rendering state
    bool wireframeMode = false;
    bool frustumCulling = true;
    uint32_t lastDrawCalls = 0;
};

} // namespace rendering
} // namespace wowee
