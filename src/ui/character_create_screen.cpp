#include "ui/character_create_screen.hpp"
#include "rendering/character_preview.hpp"
#include "game/game_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include <imgui.h>
#include <cstring>

namespace wowee {
namespace ui {

static const game::Race allRaces[] = {
    // Alliance
    game::Race::HUMAN, game::Race::DWARF, game::Race::NIGHT_ELF,
    game::Race::GNOME, game::Race::DRAENEI,
    // Horde
    game::Race::ORC, game::Race::UNDEAD, game::Race::TAUREN,
    game::Race::TROLL, game::Race::BLOOD_ELF,
};
static constexpr int RACE_COUNT = 10;
static constexpr int ALLIANCE_COUNT = 5;

static const game::Class allClasses[] = {
    game::Class::WARRIOR, game::Class::PALADIN, game::Class::HUNTER,
    game::Class::ROGUE, game::Class::PRIEST, game::Class::DEATH_KNIGHT,
    game::Class::SHAMAN, game::Class::MAGE, game::Class::WARLOCK,
    game::Class::DRUID,
};

CharacterCreateScreen::CharacterCreateScreen() {
    reset();
}

CharacterCreateScreen::~CharacterCreateScreen() = default;

void CharacterCreateScreen::reset() {
    std::memset(nameBuffer, 0, sizeof(nameBuffer));
    raceIndex = 0;
    classIndex = 0;
    genderIndex = 0;
    skin = 0;
    face = 0;
    hairStyle = 0;
    hairColor = 0;
    facialHair = 0;
    maxSkin = 9;
    maxFace = 9;
    maxHairStyle = 11;
    maxHairColor = 9;
    maxFacialHair = 8;
    statusMessage.clear();
    statusIsError = false;
    updateAvailableClasses();

    // Reset preview tracking to force model reload on next render
    prevRaceIndex_ = -1;
    prevGenderIndex_ = -1;
    prevSkin_ = -1;
    prevFace_ = -1;
    prevHairStyle_ = -1;
    prevHairColor_ = -1;
    prevFacialHair_ = -1;
    prevRangeRace_ = -1;
    prevRangeGender_ = -1;
    prevRangeSkin_ = -1;
    prevRangeHairStyle_ = -1;
}

void CharacterCreateScreen::initializePreview(pipeline::AssetManager* am) {
    assetManager_ = am;
    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        preview_->initialize(am);
    }
    // Force model reload
    prevRaceIndex_ = -1;
}

void CharacterCreateScreen::update(float deltaTime) {
    if (preview_) {
        preview_->update(deltaTime);
    }
}

void CharacterCreateScreen::setStatus(const std::string& msg, bool isError) {
    statusMessage = msg;
    statusIsError = isError;
}

void CharacterCreateScreen::updateAvailableClasses() {
    availableClasses.clear();
    game::Race race = allRaces[raceIndex];
    for (auto cls : allClasses) {
        if (game::isValidRaceClassCombo(race, cls)) {
            availableClasses.push_back(cls);
        }
    }
    // Clamp class index
    if (classIndex >= static_cast<int>(availableClasses.size())) {
        classIndex = 0;
    }
}

void CharacterCreateScreen::updatePreviewIfNeeded() {
    if (!preview_) return;

    bool changed = (raceIndex != prevRaceIndex_ ||
                    genderIndex != prevGenderIndex_ ||
                    bodyTypeIndex != prevBodyTypeIndex_ ||
                    skin != prevSkin_ ||
                    face != prevFace_ ||
                    hairStyle != prevHairStyle_ ||
                    hairColor != prevHairColor_ ||
                    facialHair != prevFacialHair_);

    if (changed) {
        bool useFemaleModel = (genderIndex == 2 && bodyTypeIndex == 1);  // Nonbinary + Feminine
        preview_->loadCharacter(
            allRaces[raceIndex],
            static_cast<game::Gender>(genderIndex),
            static_cast<uint8_t>(skin),
            static_cast<uint8_t>(face),
            static_cast<uint8_t>(hairStyle),
            static_cast<uint8_t>(hairColor),
            static_cast<uint8_t>(facialHair),
            useFemaleModel);

        prevRaceIndex_ = raceIndex;
        prevGenderIndex_ = genderIndex;
        prevBodyTypeIndex_ = bodyTypeIndex;
        prevSkin_ = skin;
        prevFace_ = face;
        prevHairStyle_ = hairStyle;
        prevHairColor_ = hairColor;
        prevFacialHair_ = facialHair;
    }
}

void CharacterCreateScreen::updateAppearanceRanges() {
    if (raceIndex == prevRangeRace_ &&
        genderIndex == prevRangeGender_ &&
        skin == prevRangeSkin_ &&
        hairStyle == prevRangeHairStyle_) {
        return;
    }

    prevRangeRace_ = raceIndex;
    prevRangeGender_ = genderIndex;
    prevRangeSkin_ = skin;
    prevRangeHairStyle_ = hairStyle;

    maxSkin = 9;
    maxFace = 9;
    maxHairStyle = 11;
    maxHairColor = 9;
    maxFacialHair = 8;

    if (!assetManager_) return;
    auto dbc = assetManager_->loadDBC("CharSections.dbc");
    if (!dbc) return;

    uint32_t targetRaceId = static_cast<uint32_t>(allRaces[raceIndex]);
    uint32_t targetSexId = (genderIndex == 1) ? 1u : 0u;

    int skinMax = -1;
    int hairStyleMax = -1;
    for (uint32_t r = 0; r < dbc->getRecordCount(); r++) {
        uint32_t raceId = dbc->getUInt32(r, 1);
        uint32_t sexId = dbc->getUInt32(r, 2);
        if (raceId != targetRaceId || sexId != targetSexId) continue;

        uint32_t baseSection = dbc->getUInt32(r, 3);
        uint32_t variationIndex = dbc->getUInt32(r, 8);
        uint32_t colorIndex = dbc->getUInt32(r, 9);

        if (baseSection == 0 && variationIndex == 0) {
            skinMax = std::max(skinMax, static_cast<int>(colorIndex));
        } else if (baseSection == 3) {
            hairStyleMax = std::max(hairStyleMax, static_cast<int>(variationIndex));
        }
    }

    if (skinMax >= 0) {
        maxSkin = skinMax;
        if (skin > maxSkin) skin = maxSkin;
    }
    if (hairStyleMax >= 0) {
        maxHairStyle = hairStyleMax;
        if (hairStyle > maxHairStyle) hairStyle = maxHairStyle;
    }

    int faceMax = -1;
    int hairColorMax = -1;
    for (uint32_t r = 0; r < dbc->getRecordCount(); r++) {
        uint32_t raceId = dbc->getUInt32(r, 1);
        uint32_t sexId = dbc->getUInt32(r, 2);
        if (raceId != targetRaceId || sexId != targetSexId) continue;

        uint32_t baseSection = dbc->getUInt32(r, 3);
        uint32_t variationIndex = dbc->getUInt32(r, 8);
        uint32_t colorIndex = dbc->getUInt32(r, 9);

        if (baseSection == 1 && colorIndex == static_cast<uint32_t>(skin)) {
            faceMax = std::max(faceMax, static_cast<int>(variationIndex));
        } else if (baseSection == 3 && variationIndex == static_cast<uint32_t>(hairStyle)) {
            hairColorMax = std::max(hairColorMax, static_cast<int>(colorIndex));
        }
    }

    if (faceMax >= 0) {
        maxFace = faceMax;
        if (face > maxFace) face = maxFace;
    }
    if (hairColorMax >= 0) {
        maxHairColor = hairColorMax;
        if (hairColor > maxHairColor) hairColor = maxHairColor;
    }
    int facialMax = -1;
    auto facialDbc = assetManager_->loadDBC("CharacterFacialHairStyles.dbc");
    if (facialDbc) {
        for (uint32_t r = 0; r < facialDbc->getRecordCount(); r++) {
            uint32_t raceId = facialDbc->getUInt32(r, 0);
            uint32_t sexId = facialDbc->getUInt32(r, 1);
            if (raceId != targetRaceId || sexId != targetSexId) continue;
            uint32_t variation = facialDbc->getUInt32(r, 2);
            facialMax = std::max(facialMax, static_cast<int>(variation));
        }
    }
    if (facialMax >= 0) {
        maxFacialHair = facialMax;
    } else if (targetSexId == 1) {
        maxFacialHair = 0;
    }
    if (facialHair > maxFacialHair) {
        facialHair = maxFacialHair;
    }
}

void CharacterCreateScreen::render(game::GameHandler& /*gameHandler*/) {
    // Render the preview to FBO before the ImGui frame
    if (preview_) {
        updatePreviewIfNeeded();
        preview_->render();
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    bool hasPreview = (preview_ && preview_->getTextureId() != 0);
    float previewWidth = hasPreview ? 320.0f : 0.0f;
    float controlsWidth = 540.0f;
    float totalWidth = hasPreview ? (previewWidth + controlsWidth + 16.0f) : 600.0f;
    float totalHeight = 580.0f;

    ImGui::SetNextWindowSize(ImVec2(totalWidth, totalHeight), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - totalWidth) * 0.5f,
                                    (displaySize.y - totalHeight) * 0.5f),
                            ImGuiCond_Always);

    ImGui::Begin("Create Character", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    if (hasPreview) {
        // Left panel: 3D preview
        ImGui::BeginChild("##preview_panel", ImVec2(previewWidth, -40.0f), false);
        {
            // Display the FBO texture (flip Y for OpenGL)
            float imgW = previewWidth - 8.0f;
            float imgH = imgW * (static_cast<float>(preview_->getHeight()) /
                                  static_cast<float>(preview_->getWidth()));
            if (imgH > totalHeight - 80.0f) {
                imgH = totalHeight - 80.0f;
                imgW = imgH * (static_cast<float>(preview_->getWidth()) /
                               static_cast<float>(preview_->getHeight()));
            }

            ImGui::Image(
                static_cast<ImTextureID>(preview_->getTextureId()),
                ImVec2(imgW, imgH),
                ImVec2(0.0f, 1.0f),  // UV top-left (flipped Y)
                ImVec2(1.0f, 0.0f)); // UV bottom-right (flipped Y)

            // Mouse drag rotation on the preview image
            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float deltaX = ImGui::GetIO().MouseDelta.x;
                preview_->rotate(deltaX * 0.2f);
            }

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drag to rotate");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel: controls
        ImGui::BeginChild("##controls_panel", ImVec2(0, -40.0f), false);
    }

    // Name input
    ImGui::Text("Name:");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer));

    ImGui::Spacing();

    // Race selection
    ImGui::Text("Race:");
    ImGui::Spacing();
    ImGui::Indent(10.0f);
    ImGui::TextColored(ImVec4(0.3f, 0.5f, 1.0f, 1.0f), "Alliance:");
    ImGui::SameLine();
    for (int i = 0; i < ALLIANCE_COUNT; ++i) {
        if (i > 0) ImGui::SameLine();
        bool selected = (raceIndex == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 1.0f, 0.8f));
        if (ImGui::SmallButton(game::getRaceName(allRaces[i]))) {
            if (raceIndex != i) {
                raceIndex = i;
                classIndex = 0;
                skin = face = hairStyle = hairColor = facialHair = 0;
                updateAvailableClasses();
            }
        }
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Horde:");
    ImGui::SameLine();
    for (int i = ALLIANCE_COUNT; i < RACE_COUNT; ++i) {
        if (i > ALLIANCE_COUNT) ImGui::SameLine();
        bool selected = (raceIndex == i);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.3f, 0.8f));
        if (ImGui::SmallButton(game::getRaceName(allRaces[i]))) {
            if (raceIndex != i) {
                raceIndex = i;
                classIndex = 0;
                skin = face = hairStyle = hairColor = facialHair = 0;
                updateAvailableClasses();
            }
        }
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::Unindent(10.0f);

    ImGui::Spacing();

    // Class selection
    ImGui::Text("Class:");
    ImGui::SameLine(80);
    if (!availableClasses.empty()) {
        ImGui::BeginGroup();
        for (int i = 0; i < static_cast<int>(availableClasses.size()); ++i) {
            if (i > 0) ImGui::SameLine();
            bool selected = (classIndex == i);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
            if (ImGui::SmallButton(game::getClassName(availableClasses[i]))) {
                classIndex = i;
            }
            if (selected) ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
    }

    ImGui::Spacing();

    // Gender
    ImGui::Text("Gender:");
    ImGui::SameLine(80);
    ImGui::RadioButton("Male", &genderIndex, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Female", &genderIndex, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Nonbinary", &genderIndex, 2);

    // Body type selection for nonbinary
    if (genderIndex == 2) {  // Nonbinary
        ImGui::Text("Body Type:");
        ImGui::SameLine(80);
        ImGui::RadioButton("Masculine", &bodyTypeIndex, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Feminine", &bodyTypeIndex, 1);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Appearance sliders
    updateAppearanceRanges();
    game::Gender currentGender = static_cast<game::Gender>(genderIndex);

    ImGui::Text("Appearance");
    ImGui::Spacing();

    float sliderWidth = hasPreview ? 180.0f : 200.0f;
    float labelCol = hasPreview ? 100.0f : 120.0f;

    auto slider = [&](const char* label, int* val, int maxVal) {
        ImGui::Text("%s", label);
        ImGui::SameLine(labelCol);
        ImGui::SetNextItemWidth(sliderWidth);
        char id[32];
        snprintf(id, sizeof(id), "##%s", label);
        ImGui::SliderInt(id, val, 0, maxVal);
    };

    slider("Skin",           &skin,      maxSkin);
    slider("Face",           &face,      maxFace);
    slider("Hair Style",     &hairStyle, maxHairStyle);
    slider("Hair Color",     &hairColor, maxHairColor);
    slider("Facial Feature", &facialHair, maxFacialHair);

    ImGui::Spacing();

    // Status message
    if (!statusMessage.empty()) {
        ImGui::Separator();
        ImGui::Spacing();
        ImVec4 color = statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", statusMessage.c_str());
    }

    if (hasPreview) {
        ImGui::EndChild(); // controls_panel
    }

    // Bottom buttons (outside children)
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Create", ImVec2(150, 35))) {
        std::string name(nameBuffer);
        // Trim whitespace
        size_t start = name.find_first_not_of(" \t\r\n");
        size_t end = name.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            name.clear();
        } else {
            name = name.substr(start, end - start + 1);
        }
        if (name.empty()) {
            setStatus("Please enter a character name.", true);
        } else if (availableClasses.empty()) {
            setStatus("No valid class for this race.", true);
        } else {
            setStatus("Creating character...", false);
            game::CharCreateData data;
            data.name = name;
            data.race = allRaces[raceIndex];
            data.characterClass = availableClasses[classIndex];
            data.gender = currentGender;
            data.useFemaleModel = (genderIndex == 2 && bodyTypeIndex == 1);  // Nonbinary + Feminine
            data.skin = static_cast<uint8_t>(skin);
            data.face = static_cast<uint8_t>(face);
            data.hairStyle = static_cast<uint8_t>(hairStyle);
            data.hairColor = static_cast<uint8_t>(hairColor);
            data.facialHair = static_cast<uint8_t>(facialHair);
            if (onCreate) {
                onCreate(data);
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Back", ImVec2(150, 35))) {
        if (onCancel) {
            onCancel();
        }
    }

    ImGui::End();
}

} // namespace ui
} // namespace wowee
