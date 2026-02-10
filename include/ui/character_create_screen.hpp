#pragma once

#include "game/character.hpp"
#include "game/world_packets.hpp"
#include <imgui.h>
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace wowee {
namespace game { class GameHandler; }
namespace pipeline { class AssetManager; }
namespace rendering { class CharacterPreview; }

namespace ui {

class CharacterCreateScreen {
public:
    CharacterCreateScreen();
    ~CharacterCreateScreen();

    void render(game::GameHandler& gameHandler);
    void update(float deltaTime);
    void setOnCreate(std::function<void(const game::CharCreateData&)> cb) { onCreate = std::move(cb); }
    void setOnCancel(std::function<void()> cb) { onCancel = std::move(cb); }
    void setStatus(const std::string& msg, bool isError = false);
    void reset();
    void initializePreview(pipeline::AssetManager* am);

private:
    char nameBuffer[13] = {};  // WoW max name = 12 chars + null
    int raceIndex = 0;
    int classIndex = 0;
    int genderIndex = 0;
    int bodyTypeIndex = 0;  // For nonbinary: 0=masculine, 1=feminine
    int skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    int maxSkin = 9, maxFace = 9, maxHairStyle = 11, maxHairColor = 9, maxFacialHair = 8;
    std::string statusMessage;
    bool statusIsError = false;

    std::vector<game::Class> availableClasses;
    void updateAvailableClasses();

    std::function<void(const game::CharCreateData&)> onCreate;
    std::function<void()> onCancel;

    // 3D model preview
    std::unique_ptr<rendering::CharacterPreview> preview_;
    pipeline::AssetManager* assetManager_ = nullptr;
    int prevRaceIndex_ = -1;
    int prevGenderIndex_ = -1;
    int prevBodyTypeIndex_ = -1;
    int prevSkin_ = -1;
    int prevFace_ = -1;
    int prevHairStyle_ = -1;
    int prevHairColor_ = -1;
    int prevFacialHair_ = -1;
    int prevRangeRace_ = -1;
    int prevRangeGender_ = -1;
    int prevRangeSkin_ = -1;
    int prevRangeHairStyle_ = -1;
    bool draggingPreview_ = false;
    float dragStartX_ = 0.0f;

    void updatePreviewIfNeeded();
    void updateAppearanceRanges();
};

} // namespace ui
} // namespace wowee
