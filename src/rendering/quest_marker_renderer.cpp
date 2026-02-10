#include "rendering/quest_marker_renderer.hpp"
#include "rendering/camera.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/blp_loader.hpp"
#include "core/logger.hpp"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL2/SDL.h>
#include <cmath>

namespace wowee { namespace rendering {

QuestMarkerRenderer::QuestMarkerRenderer() {
}

QuestMarkerRenderer::~QuestMarkerRenderer() {
    shutdown();
}

bool QuestMarkerRenderer::initialize(pipeline::AssetManager* assetManager) {
    if (!assetManager) {
        LOG_WARNING("QuestMarkerRenderer: No AssetManager provided");
        return false;
    }

    LOG_INFO("QuestMarkerRenderer: Initializing...");
    createShader();
    createQuad();
    loadTextures(assetManager);
    LOG_INFO("QuestMarkerRenderer: Initialization complete");

    return true;
}

void QuestMarkerRenderer::shutdown() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (shaderProgram_) glDeleteProgram(shaderProgram_);
    for (int i = 0; i < 3; ++i) {
        if (textures_[i]) glDeleteTextures(1, &textures_[i]);
    }
    markers_.clear();
}

void QuestMarkerRenderer::createQuad() {
    // Billboard quad vertices (centered, 1 unit size)
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,  // bottom-left
         0.5f, -0.5f, 0.0f,  1.0f, 1.0f,  // bottom-right
         0.5f,  0.5f, 0.0f,  1.0f, 0.0f,  // top-right
        -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,  // top-left
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,  // bottom-left
         0.5f,  0.5f, 0.0f,  1.0f, 0.0f   // top-right
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void QuestMarkerRenderer::loadTextures(pipeline::AssetManager* assetManager) {
    const char* paths[3] = {
        "Interface\\GossipFrame\\AvailableQuestIcon.blp",
        "Interface\\GossipFrame\\ActiveQuestIcon.blp",
        "Interface\\GossipFrame\\IncompleteQuestIcon.blp"
    };

    for (int i = 0; i < 3; ++i) {
        pipeline::BLPImage blp = assetManager->loadTexture(paths[i]);
        if (!blp.isValid()) {
            LOG_WARNING("Failed to load quest marker texture: ", paths[i]);
            continue;
        }

        glGenTextures(1, &textures_[i]);
        glBindTexture(GL_TEXTURE_2D, textures_[i]);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blp.width, blp.height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, blp.data.data());

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_2D);

        LOG_INFO("Loaded quest marker texture: ", paths[i]);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void QuestMarkerRenderer::createShader() {
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;

        out vec2 TexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D markerTexture;
        uniform float uAlpha;

        void main() {
            vec4 texColor = texture(markerTexture, TexCoord);
            if (texColor.a < 0.1)
                discard;
            FragColor = vec4(texColor.rgb, texColor.a * uAlpha);
        }
    )";

    // Compile vertex shader
    uint32_t vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    // Compile fragment shader
    uint32_t fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    // Link shader program
    shaderProgram_ = glCreateProgram();
    glAttachShader(shaderProgram_, vertexShader);
    glAttachShader(shaderProgram_, fragmentShader);
    glLinkProgram(shaderProgram_);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void QuestMarkerRenderer::setMarker(uint64_t guid, const glm::vec3& position, int markerType, float boundingHeight) {
    markers_[guid] = {position, markerType, boundingHeight};
}

void QuestMarkerRenderer::removeMarker(uint64_t guid) {
    markers_.erase(guid);
}

void QuestMarkerRenderer::clear() {
    markers_.clear();
}

void QuestMarkerRenderer::render(const Camera& camera) {
    if (markers_.empty() || !shaderProgram_ || !vao_) return;

    // WoW-style quest marker tuning parameters
    constexpr float BASE_SIZE = 0.65f;          // Base world-space size
    constexpr float HEIGHT_OFFSET = 1.1f;       // Height above NPC bounds
    constexpr float BOB_AMPLITUDE = 0.10f;      // Bob animation amplitude
    constexpr float BOB_FREQUENCY = 1.25f;      // Bob frequency (Hz)
    constexpr float MIN_DIST = 4.0f;            // Near clamp
    constexpr float MAX_DIST = 90.0f;           // Far fade-out start
    constexpr float FADE_RANGE = 25.0f;         // Fade-out range
    constexpr float GLOW_ALPHA = 0.35f;         // Glow pass alpha

    // Get time for bob animation
    float timeSeconds = SDL_GetTicks() / 1000.0f;

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Don't write to depth buffer

    glUseProgram(shaderProgram_);

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix();
    glm::vec3 cameraPos = camera.getPosition();

    int viewLoc = glGetUniformLocation(shaderProgram_, "view");
    int projLoc = glGetUniformLocation(shaderProgram_, "projection");
    int modelLoc = glGetUniformLocation(shaderProgram_, "model");
    int alphaLoc = glGetUniformLocation(shaderProgram_, "uAlpha");

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glBindVertexArray(vao_);

    // Get camera right and up vectors for billboarding
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    for (const auto& [guid, marker] : markers_) {
        if (marker.type < 0 || marker.type > 2) continue;
        if (!textures_[marker.type]) continue;

        // Calculate distance for LOD and culling
        glm::vec3 toCamera = cameraPos - marker.position;
        float dist = glm::length(toCamera);

        // Calculate fade alpha
        float fadeAlpha = 1.0f;
        if (dist > MAX_DIST) {
            float t = glm::clamp((dist - MAX_DIST) / FADE_RANGE, 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t); // Smoothstep
            fadeAlpha = 1.0f - t;
        }
        if (fadeAlpha <= 0.001f) continue; // Cull if fully faded

        // Distance-based scaling (mild compensation for readability)
        float distScale = 1.0f;
        if (dist > MIN_DIST) {
            float t = glm::clamp((dist - 5.0f) / 55.0f, 0.0f, 1.0f);
            distScale = 1.0f + 0.35f * t;
        }
        float size = BASE_SIZE * distScale;
        size = glm::clamp(size, BASE_SIZE * 0.9f, BASE_SIZE * 1.6f);

        // Bob animation
        float bob = std::sin(timeSeconds * BOB_FREQUENCY * 2.0f * 3.14159f) * BOB_AMPLITUDE;

        // Position marker above NPC with bob
        glm::vec3 markerPos = marker.position;
        markerPos.z += marker.boundingHeight + HEIGHT_OFFSET + bob;

        // Build billboard matrix (camera-facing quad)
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, markerPos);

        // Billboard: align quad to face camera
        model[0] = glm::vec4(cameraRight * size, 0.0f);
        model[1] = glm::vec4(cameraUp * size, 0.0f);
        model[2] = glm::vec4(glm::cross(cameraRight, cameraUp), 0.0f);

        glBindTexture(GL_TEXTURE_2D, textures_[marker.type]);

        // Glow pass (subtle additive glow for available/turnin markers)
        if (marker.type == 0 || marker.type == 1) { // Available or turnin
            glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending
            glUniform1f(alphaLoc, fadeAlpha * GLOW_ALPHA); // Reduced alpha for glow
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // Restore standard alpha blending for main pass
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // Main pass with fade alpha
        glUniform1f(alphaLoc, fadeAlpha);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}} // namespace wowee::rendering
