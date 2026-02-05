#include "rendering/minimap.hpp"
#include "rendering/shader.hpp"
#include "rendering/camera.hpp"
#include "rendering/terrain_renderer.hpp"
#include "core/logger.hpp"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

namespace wowee {
namespace rendering {

Minimap::Minimap() = default;

Minimap::~Minimap() {
    shutdown();
}

bool Minimap::initialize(int size) {
    mapSize = size;

    // Create FBO
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color texture
    glGenTextures(1, &fboTexture);
    glBindTexture(GL_TEXTURE_2D, fboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mapSize, mapSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &fboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mapSize, mapSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Minimap FBO incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Screen quad (NDC fullscreen, we'll position via uniforms)
    float quadVerts[] = {
        // pos (x,y), uv (u,v)
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    // Quad shader with circular mask and border
    const char* vertSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUV;

        uniform vec4 uRect; // x, y, w, h in NDC

        out vec2 TexCoord;

        void main() {
            vec2 pos = uRect.xy + aUV * uRect.zw;
            gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
            TexCoord = aUV;
        }
    )";

    const char* fragSrc = R"(
        #version 330 core
        in vec2 TexCoord;

        uniform sampler2D uMapTexture;

        out vec4 FragColor;

        void main() {
            vec2 center = TexCoord - vec2(0.5);
            float dist = length(center);

            // Circular mask
            if (dist > 0.5) discard;

            // Gold border ring
            float borderWidth = 0.02;
            if (dist > 0.5 - borderWidth) {
                FragColor = vec4(0.8, 0.65, 0.2, 1.0);
                return;
            }

            vec4 texColor = texture(uMapTexture, TexCoord);

            // Player dot at center
            if (dist < 0.02) {
                FragColor = vec4(1.0, 0.3, 0.3, 1.0);
                return;
            }

            FragColor = texColor;
        }
    )";

    quadShader = std::make_unique<Shader>();
    if (!quadShader->loadFromSource(vertSrc, fragSrc)) {
        LOG_ERROR("Failed to create minimap shader");
        return false;
    }

    LOG_INFO("Minimap initialized (", mapSize, "x", mapSize, ")");
    return true;
}

void Minimap::shutdown() {
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (fboTexture) { glDeleteTextures(1, &fboTexture); fboTexture = 0; }
    if (fboDepth) { glDeleteRenderbuffers(1, &fboDepth); fboDepth = 0; }
    if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
    if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
    quadShader.reset();
}

void Minimap::render(const Camera& playerCamera, const glm::vec3& centerWorldPos,
                     int screenWidth, int screenHeight) {
    if (!enabled || !terrainRenderer || !fbo) return;

    const auto now = std::chrono::steady_clock::now();
    glm::vec3 playerPos = centerWorldPos;
    bool needsRefresh = !hasCachedFrame;
    if (!needsRefresh) {
        float moved = glm::length(glm::vec2(playerPos.x - lastUpdatePos.x, playerPos.y - lastUpdatePos.y));
        float elapsed = std::chrono::duration<float>(now - lastUpdateTime).count();
        needsRefresh = (moved >= updateDistance) || (elapsed >= updateIntervalSec);
    }

    // 1. Render terrain from top-down into FBO (throttled)
    if (needsRefresh) {
        renderTerrainToFBO(playerCamera, centerWorldPos);
        lastUpdateTime = now;
        lastUpdatePos = playerPos;
        hasCachedFrame = true;
    }

    // 2. Draw the minimap quad on screen
    renderQuad(screenWidth, screenHeight);
}

void Minimap::renderTerrainToFBO(const Camera& /*playerCamera*/, const glm::vec3& centerWorldPos) {
    // Save current viewport
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, mapSize, mapSize);
    glClearColor(0.05f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Create a top-down camera at the player's XY position
    Camera topDownCamera;
    glm::vec3 playerPos = centerWorldPos;
    topDownCamera.setPosition(glm::vec3(playerPos.x, playerPos.y, playerPos.z + 5000.0f));
    topDownCamera.setRotation(0.0f, -89.9f);  // Look straight down
    topDownCamera.setAspectRatio(1.0f);
    topDownCamera.setFov(1.0f);  // Will be overridden by ortho below

    // We need orthographic projection, but Camera only supports perspective.
    // Use the terrain renderer's render with a custom view/projection.
    // For now, render with the top-down camera (perspective, narrow FOV approximates ortho)
    // The narrow FOV + high altitude gives a near-orthographic result.

    // Calculate FOV that covers viewRadius at the altitude
    float altitude = 5000.0f;
    float fovDeg = glm::degrees(2.0f * std::atan(viewRadius / altitude));
    topDownCamera.setFov(fovDeg);

    terrainRenderer->render(topDownCamera);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore viewport
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void Minimap::renderQuad(int screenWidth, int screenHeight) {
    glDisable(GL_DEPTH_TEST);

    quadShader->use();

    // Position minimap in top-right corner with margin
    float margin = 10.0f;
    float pixelW = static_cast<float>(mapSize) / screenWidth;
    float pixelH = static_cast<float>(mapSize) / screenHeight;
    float x = 1.0f - pixelW - margin / screenWidth;
    float y = 1.0f - pixelH - margin / screenHeight;

    // uRect: x, y, w, h in 0..1 screen space
    quadShader->setUniform("uRect", glm::vec4(x, y, pixelW, pixelH));
    quadShader->setUniform("uMapTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fboTexture);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

} // namespace rendering
} // namespace wowee
