#pragma once

#include "game/character.hpp"
#include <GL/glew.h>
#include <memory>
#include <cstdint>

namespace wowee {
namespace pipeline { class AssetManager; }
namespace rendering {

class CharacterRenderer;
class Camera;

class CharacterPreview {
public:
    CharacterPreview();
    ~CharacterPreview();

    bool initialize(pipeline::AssetManager* am);
    void shutdown();

    bool loadCharacter(game::Race race, game::Gender gender,
                       uint8_t skin, uint8_t face,
                       uint8_t hairStyle, uint8_t hairColor,
                       uint8_t facialHair);

    void update(float deltaTime);
    void render();
    void rotate(float yawDelta);

    GLuint getTextureId() const { return colorTexture_; }
    int getWidth() const { return fboWidth_; }
    int getHeight() const { return fboHeight_; }

    CharacterRenderer* getCharacterRenderer() { return charRenderer_.get(); }
    uint32_t getInstanceId() const { return instanceId_; }
    uint32_t getModelId() const { return PREVIEW_MODEL_ID; }
    bool isModelLoaded() const { return modelLoaded_; }

private:
    void createFBO();
    void destroyFBO();

    pipeline::AssetManager* assetManager_ = nullptr;
    std::unique_ptr<CharacterRenderer> charRenderer_;
    std::unique_ptr<Camera> camera_;

    GLuint fbo_ = 0;
    GLuint colorTexture_ = 0;
    GLuint depthRenderbuffer_ = 0;
    static constexpr int fboWidth_ = 400;
    static constexpr int fboHeight_ = 500;

    static constexpr uint32_t PREVIEW_MODEL_ID = 9999;
    uint32_t instanceId_ = 0;
    bool modelLoaded_ = false;
    float modelYaw_ = 180.0f;
};

} // namespace rendering
} // namespace wowee
