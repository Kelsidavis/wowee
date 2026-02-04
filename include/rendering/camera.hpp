#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wowee {
namespace rendering {

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

class Camera {
public:
    Camera();

    void setPosition(const glm::vec3& pos) { position = pos; updateViewMatrix(); }
    void setRotation(float yaw, float pitch) { this->yaw = yaw; this->pitch = pitch; updateViewMatrix(); }
    void setAspectRatio(float aspect) { aspectRatio = aspect; updateProjectionMatrix(); }
    void setFov(float fov) { this->fov = fov; updateProjectionMatrix(); }

    const glm::vec3& getPosition() const { return position; }
    const glm::mat4& getViewMatrix() const { return viewMatrix; }
    const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
    glm::mat4 getViewProjectionMatrix() const { return projectionMatrix * viewMatrix; }
    float getAspectRatio() const { return aspectRatio; }

    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;

    Ray screenToWorldRay(float screenX, float screenY, float screenW, float screenH) const;

private:
    void updateViewMatrix();
    void updateProjectionMatrix();

    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fov = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.05f;
    float farPlane = 30000.0f;   // Improves depth precision vs extremely large far clip

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
};

} // namespace rendering
} // namespace wowee
