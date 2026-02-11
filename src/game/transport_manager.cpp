#include "game/transport_manager.hpp"
#include "rendering/wmo_renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <iostream>

namespace wowee::game {

TransportManager::TransportManager() = default;
TransportManager::~TransportManager() = default;

void TransportManager::update(float deltaTime) {
    for (auto& [guid, transport] : transports_) {
        updateTransportMovement(transport, deltaTime);
    }
}

void TransportManager::registerTransport(uint64_t guid, uint32_t wmoInstanceId, uint32_t pathId) {
    auto pathIt = paths_.find(pathId);
    if (pathIt == paths_.end()) {
        std::cerr << "TransportManager: Path " << pathId << " not found for transport " << guid << std::endl;
        return;
    }

    const auto& path = pathIt->second;
    if (path.waypoints.empty()) {
        std::cerr << "TransportManager: Path " << pathId << " has no waypoints" << std::endl;
        return;
    }

    ActiveTransport transport;
    transport.guid = guid;
    transport.wmoInstanceId = wmoInstanceId;
    transport.pathId = pathId;
    transport.currentSegment = 0;
    transport.segmentProgress = 0.0f;
    transport.position = path.waypoints[0];
    transport.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion
    transport.playerOnBoard = false;
    transport.playerLocalOffset = glm::vec3(0.0f);
    transport.hasDeckBounds = false;

    updateTransformMatrices(transport);

    transports_[guid] = transport;

    std::cout << "TransportManager: Registered transport " << guid
              << " at path " << pathId << " with " << path.waypoints.size() << " waypoints" << std::endl;
}

void TransportManager::unregisterTransport(uint64_t guid) {
    transports_.erase(guid);
    std::cout << "TransportManager: Unregistered transport " << guid << std::endl;
}

ActiveTransport* TransportManager::getTransport(uint64_t guid) {
    auto it = transports_.find(guid);
    if (it != transports_.end()) {
        return &it->second;
    }
    return nullptr;
}

glm::vec3 TransportManager::getPlayerWorldPosition(uint64_t transportGuid, const glm::vec3& localOffset) {
    auto* transport = getTransport(transportGuid);
    if (!transport) {
        return localOffset;  // Fallback
    }

    glm::vec4 localPos(localOffset, 1.0f);
    glm::vec4 worldPos = transport->transform * localPos;
    return glm::vec3(worldPos);
}

glm::mat4 TransportManager::getTransportInvTransform(uint64_t transportGuid) {
    auto* transport = getTransport(transportGuid);
    if (!transport) {
        return glm::mat4(1.0f);  // Identity fallback
    }
    return transport->invTransform;
}

void TransportManager::loadPathFromNodes(uint32_t pathId, const std::vector<glm::vec3>& waypoints, bool looping, float speed) {
    if (waypoints.empty()) {
        std::cerr << "TransportManager: Cannot load empty path " << pathId << std::endl;
        return;
    }

    TransportPath path;
    path.pathId = pathId;
    path.waypoints = waypoints;
    path.looping = looping;
    path.speed = speed;

    paths_[pathId] = path;

    std::cout << "TransportManager: Loaded path " << pathId
              << " with " << waypoints.size() << " waypoints, "
              << "looping=" << looping << ", speed=" << speed << std::endl;
}

void TransportManager::setDeckBounds(uint64_t guid, const glm::vec3& min, const glm::vec3& max) {
    auto* transport = getTransport(guid);
    if (!transport) {
        std::cerr << "TransportManager: Cannot set deck bounds for unknown transport " << guid << std::endl;
        return;
    }

    transport->deckMin = min;
    transport->deckMax = max;
    transport->hasDeckBounds = true;
}

void TransportManager::updateTransportMovement(ActiveTransport& transport, float deltaTime) {
    auto pathIt = paths_.find(transport.pathId);
    if (pathIt == paths_.end()) {
        return;
    }

    const auto& path = pathIt->second;
    if (path.waypoints.size() < 2) {
        return;  // Need at least 2 waypoints to move
    }

    // Calculate segment length
    glm::vec3 p0 = path.waypoints[transport.currentSegment];
    size_t nextIdx = (transport.currentSegment + 1) % path.waypoints.size();
    glm::vec3 p1 = path.waypoints[nextIdx];
    float segmentLength = glm::distance(p0, p1);

    if (segmentLength < 0.001f) {
        // Zero-length segment, skip to next
        transport.currentSegment = nextIdx;
        transport.segmentProgress = 0.0f;
        return;
    }

    // Update progress
    float distanceThisFrame = path.speed * deltaTime;
    transport.segmentProgress += distanceThisFrame;

    // Check if we've completed this segment
    while (transport.segmentProgress >= segmentLength) {
        transport.segmentProgress -= segmentLength;
        transport.currentSegment = nextIdx;

        // Check for path completion
        if (!path.looping && transport.currentSegment >= path.waypoints.size() - 1) {
            // Reached end of non-looping path
            transport.currentSegment = path.waypoints.size() - 1;
            transport.segmentProgress = 0.0f;
            transport.position = path.waypoints[transport.currentSegment];
            updateTransformMatrices(transport);
            return;
        }

        // Update for next segment
        p0 = path.waypoints[transport.currentSegment];
        nextIdx = (transport.currentSegment + 1) % path.waypoints.size();
        p1 = path.waypoints[nextIdx];
        segmentLength = glm::distance(p0, p1);

        if (segmentLength < 0.001f) {
            transport.segmentProgress = 0.0f;
            continue;
        }
    }

    // Interpolate position
    float t = transport.segmentProgress / segmentLength;
    transport.position = interpolatePath(path, transport.currentSegment, t);

    // Calculate orientation from path tangent
    transport.rotation = calculateOrientation(path, transport.currentSegment, t);

    // Update transform matrices
    updateTransformMatrices(transport);

    // Update WMO instance position
    if (wmoRenderer_) {
        wmoRenderer_->setInstanceTransform(transport.wmoInstanceId, transport.transform);
    }
}

glm::vec3 TransportManager::interpolatePath(const TransportPath& path, size_t segmentIdx, float t) {
    // Catmull-Rom spline interpolation (same as taxi flights)
    size_t numPoints = path.waypoints.size();

    // Get 4 control points for Catmull-Rom
    size_t p0Idx = (segmentIdx == 0) ? (path.looping ? numPoints - 1 : 0) : segmentIdx - 1;
    size_t p1Idx = segmentIdx;
    size_t p2Idx = (segmentIdx + 1) % numPoints;
    size_t p3Idx = (segmentIdx + 2) % numPoints;

    // If non-looping and at boundaries, clamp indices
    if (!path.looping) {
        if (segmentIdx == 0) p0Idx = 0;
        if (segmentIdx >= numPoints - 2) p3Idx = numPoints - 1;
        if (segmentIdx >= numPoints - 1) p2Idx = numPoints - 1;
    }

    glm::vec3 p0 = path.waypoints[p0Idx];
    glm::vec3 p1 = path.waypoints[p1Idx];
    glm::vec3 p2 = path.waypoints[p2Idx];
    glm::vec3 p3 = path.waypoints[p3Idx];

    // Catmull-Rom spline formula
    float t2 = t * t;
    float t3 = t2 * t;

    glm::vec3 result = 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );

    return result;
}

glm::quat TransportManager::calculateOrientation(const TransportPath& path, size_t segmentIdx, float t) {
    // Calculate tangent vector for orientation
    size_t numPoints = path.waypoints.size();

    // Get 4 control points
    size_t p0Idx = (segmentIdx == 0) ? (path.looping ? numPoints - 1 : 0) : segmentIdx - 1;
    size_t p1Idx = segmentIdx;
    size_t p2Idx = (segmentIdx + 1) % numPoints;
    size_t p3Idx = (segmentIdx + 2) % numPoints;

    if (!path.looping) {
        if (segmentIdx == 0) p0Idx = 0;
        if (segmentIdx >= numPoints - 2) p3Idx = numPoints - 1;
        if (segmentIdx >= numPoints - 1) p2Idx = numPoints - 1;
    }

    glm::vec3 p0 = path.waypoints[p0Idx];
    glm::vec3 p1 = path.waypoints[p1Idx];
    glm::vec3 p2 = path.waypoints[p2Idx];
    glm::vec3 p3 = path.waypoints[p3Idx];

    // Tangent of Catmull-Rom spline (derivative)
    float t2 = t * t;
    glm::vec3 tangent = 0.5f * (
        (-p0 + p2) +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * 2.0f * t +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * 3.0f * t2
    );

    // Normalize tangent
    float tangentLength = glm::length(tangent);
    if (tangentLength < 0.001f) {
        // Fallback to simple direction
        tangent = p2 - p1;
        tangentLength = glm::length(tangent);
    }

    if (tangentLength < 0.001f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity
    }

    tangent /= tangentLength;

    // Calculate rotation from forward direction
    // WoW forward is typically +Y, but we'll use the tangent as forward
    glm::vec3 forward = tangent;
    glm::vec3 up(0.0f, 0.0f, 1.0f);  // WoW Z is up

    // If forward is nearly vertical, use different up vector
    if (std::abs(forward.z) > 0.99f) {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    up = glm::cross(forward, right);

    // Build rotation matrix and convert to quaternion
    glm::mat3 rotMat;
    rotMat[0] = right;
    rotMat[1] = forward;
    rotMat[2] = up;

    return glm::quat_cast(rotMat);
}

void TransportManager::updateTransformMatrices(ActiveTransport& transport) {
    // Build transform matrix: translate * rotate * scale
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), transport.position);
    glm::mat4 rotation = glm::mat4_cast(transport.rotation);
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));  // No scaling for transports

    transport.transform = translation * rotation * scale;
    transport.invTransform = glm::inverse(transport.transform);
}

} // namespace wowee::game
