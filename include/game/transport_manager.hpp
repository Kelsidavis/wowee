#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wowee::rendering {
    class WMORenderer;
}

namespace wowee::game {

struct TransportPath {
    uint32_t pathId;
    std::vector<glm::vec3> waypoints;  // Position keyframes
    std::vector<glm::quat> rotations;  // Optional rotation keyframes
    bool looping;
    float speed;  // units/sec (default 18.0f like taxi)
};

struct ActiveTransport {
    uint64_t guid;              // Entity GUID
    uint32_t wmoInstanceId;     // WMO renderer instance ID
    uint32_t pathId;            // Current path
    size_t currentSegment;      // Current waypoint index
    float segmentProgress;      // Distance along segment
    glm::vec3 position;         // Current world position
    glm::quat rotation;         // Current world rotation
    glm::mat4 transform;        // Cached world transform
    glm::mat4 invTransform;     // Cached inverse for collision

    // Player attachment (single-player for now)
    bool playerOnBoard;
    glm::vec3 playerLocalOffset;

    // Optional deck boundaries
    glm::vec3 deckMin;
    glm::vec3 deckMax;
    bool hasDeckBounds;
};

class TransportManager {
public:
    TransportManager();
    ~TransportManager();

    void setWMORenderer(rendering::WMORenderer* renderer) { wmoRenderer_ = renderer; }

    void update(float deltaTime);
    void registerTransport(uint64_t guid, uint32_t wmoInstanceId, uint32_t pathId);
    void unregisterTransport(uint64_t guid);

    ActiveTransport* getTransport(uint64_t guid);
    glm::vec3 getPlayerWorldPosition(uint64_t transportGuid, const glm::vec3& localOffset);
    glm::mat4 getTransportInvTransform(uint64_t transportGuid);

    void loadPathFromNodes(uint32_t pathId, const std::vector<glm::vec3>& waypoints, bool looping = true, float speed = 18.0f);
    void setDeckBounds(uint64_t guid, const glm::vec3& min, const glm::vec3& max);

private:
    void updateTransportMovement(ActiveTransport& transport, float deltaTime);
    glm::vec3 interpolatePath(const TransportPath& path, size_t segmentIdx, float t);
    glm::quat calculateOrientation(const TransportPath& path, size_t segmentIdx, float t);
    void updateTransformMatrices(ActiveTransport& transport);

    std::unordered_map<uint64_t, ActiveTransport> transports_;
    std::unordered_map<uint32_t, TransportPath> paths_;
    rendering::WMORenderer* wmoRenderer_ = nullptr;
};

} // namespace wowee::game
