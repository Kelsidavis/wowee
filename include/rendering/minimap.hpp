#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <chrono>
#include <memory>

namespace wowee {
namespace rendering {

class Shader;
class Camera;
class TerrainRenderer;

class Minimap {
public:
    Minimap();
    ~Minimap();

    bool initialize(int size = 200);
    void shutdown();

    void setTerrainRenderer(TerrainRenderer* tr) { terrainRenderer = tr; }

    void render(const Camera& playerCamera, const glm::vec3& centerWorldPos,
                int screenWidth, int screenHeight);

    void setEnabled(bool enabled) { this->enabled = enabled; }
    bool isEnabled() const { return enabled; }
    void toggle() { enabled = !enabled; }

    void setViewRadius(float radius) { viewRadius = radius; }

private:
    void renderTerrainToFBO(const Camera& playerCamera, const glm::vec3& centerWorldPos);
    void renderQuad(int screenWidth, int screenHeight);

    TerrainRenderer* terrainRenderer = nullptr;

    // FBO for offscreen rendering
    GLuint fbo = 0;
    GLuint fboTexture = 0;
    GLuint fboDepth = 0;

    // Screen quad
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
    std::unique_ptr<Shader> quadShader;

    int mapSize = 200;
    float viewRadius = 500.0f;
    bool enabled = false;
    float updateIntervalSec = 0.25f;
    float updateDistance = 6.0f;
    std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::time_point{};
    glm::vec3 lastUpdatePos = glm::vec3(0.0f);
    bool hasCachedFrame = false;
};

} // namespace rendering
} // namespace wowee
