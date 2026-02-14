#include "ui/character_screen.hpp"
#include "rendering/character_preview.hpp"
#include "pipeline/asset_manager.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include <imgui.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace wowee { namespace ui {

CharacterScreen::CharacterScreen() {
}

static uint64_t hashEquipment(const std::vector<game::EquipmentItem>& eq) {
    // FNV-1a 64-bit over (displayModel, inventoryType, enchantment)
    uint64_t h = 1469598103934665603ull;
    auto mix8 = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    auto mix32 = [&](uint32_t v) {
        mix8(static_cast<uint8_t>(v & 0xFF));
        mix8(static_cast<uint8_t>((v >> 8) & 0xFF));
        mix8(static_cast<uint8_t>((v >> 16) & 0xFF));
        mix8(static_cast<uint8_t>((v >> 24) & 0xFF));
    };
    for (const auto& it : eq) {
        mix32(it.displayModel);
        mix8(it.inventoryType);
        mix32(it.enchantment);
    }
    return h;
}

void CharacterScreen::render(game::GameHandler& gameHandler) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pad(24.0f, 24.0f);
    ImVec2 winSize(vp->Size.x - pad.x * 2.0f, vp->Size.y - pad.y * 2.0f);
    if (winSize.x < 860.0f) winSize.x = 860.0f;
    if (winSize.y < 620.0f) winSize.y = 620.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x - winSize.x) * 0.5f,
                                   vp->Pos.y + (vp->Size.y - winSize.y) * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

    ImGui::Begin("Character Selection", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // Ensure we can render a preview even if the state transition hook didn't inject the AssetManager.
    if (!assetManager_) {
        assetManager_ = core::Application::getInstance().getAssetManager();
    }

    // Get character list
    const auto& characters = gameHandler.getCharacters();

    // Request character list if not available.
    // Also show a loading state while CHAR_LIST_REQUESTED is in-flight (characters may be cleared to avoid stale UI).
    if (characters.empty() &&
        (gameHandler.getState() == game::WorldState::READY ||
         gameHandler.getState() == game::WorldState::CHAR_LIST_REQUESTED)) {
        ImGui::Text("Loading characters...");
        if (gameHandler.getState() == game::WorldState::READY) {
            gameHandler.requestCharacterList();
        }
        ImGui::End();
        return;
    }

    if (characters.empty()) {
        ImGui::Text("No characters available.");
        // Bottom buttons even when empty
        ImGui::Spacing();
        if (ImGui::Button("Back", ImVec2(120, 36))) { if (onBack) onBack(); }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(120, 36))) {
            if (gameHandler.getState() == game::WorldState::READY ||
                gameHandler.getState() == game::WorldState::CHAR_LIST_RECEIVED) {
                gameHandler.requestCharacterList();
                setStatus("Refreshing character list...");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Character", ImVec2(160, 36))) { if (onCreateCharacter) onCreateCharacter(); }
        ImGui::End();
        return;
    }

    // If the list refreshed, keep selection stable by GUID.
    if (selectedCharacterGuid != 0) {
        const bool needReselect =
            (selectedCharacterIndex < 0) ||
            (selectedCharacterIndex >= static_cast<int>(characters.size())) ||
            (characters[static_cast<size_t>(selectedCharacterIndex)].guid != selectedCharacterGuid);
        if (needReselect) {
            for (size_t i = 0; i < characters.size(); ++i) {
                if (characters[i].guid == selectedCharacterGuid) {
                    selectedCharacterIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Restore last-selected character (once per screen visit)
    if (!restoredLastCharacter) {
        // Priority 1: Select newly created character if set
        if (!newlyCreatedCharacterName.empty()) {
            for (size_t i = 0; i < characters.size(); ++i) {
                if (characters[i].name == newlyCreatedCharacterName) {
                    selectedCharacterIndex = static_cast<int>(i);
                    selectedCharacterGuid = characters[i].guid;
                    saveLastCharacter(characters[i].guid);
                    newlyCreatedCharacterName.clear();
                    break;
                }
            }
        }
        // Priority 2: Restore last selected character
        if (selectedCharacterIndex < 0) {
            uint64_t lastGuid = loadLastCharacter();
            if (lastGuid != 0) {
                for (size_t i = 0; i < characters.size(); ++i) {
                    if (characters[i].guid == lastGuid) {
                        selectedCharacterIndex = static_cast<int>(i);
                        selectedCharacterGuid = lastGuid;
                        break;
                    }
                }
            }
        }
        // Fall back to first character if nothing matched
        if (selectedCharacterIndex < 0) {
            selectedCharacterIndex = 0;
            selectedCharacterGuid = characters[0].guid;
        }
        restoredLastCharacter = true;
    }

    // Status message
    if (!statusMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", statusMessage.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // ── Two-column layout: character list (left) | details (right) ──
    float availW = ImGui::GetContentRegionAvail().x;
    float detailPanelW = 360.0f;
    float listW = availW - detailPanelW - ImGui::GetStyle().ItemSpacing.x;
    if (listW < 300.0f) { listW = availW; detailPanelW = 0.0f; }

    float listH = ImGui::GetContentRegionAvail().y - 50.0f; // reserve bottom row for buttons

    // ── Left: Character list ──
    ImGui::BeginChild("CharList", ImVec2(listW, listH), true);
    ImGui::Text("Characters");
    ImGui::Separator();

    if (ImGui::BeginTable("CharactersTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("Race",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Zone",  ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < characters.size(); ++i) {
            const auto& character = characters[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            bool isSelected = (selectedCharacterIndex == static_cast<int>(i));
            ImVec4 factionColor = getFactionColor(character.race);
            ImGui::PushStyleColor(ImGuiCol_Text, factionColor);

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(character.name.c_str(), isSelected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                selectedCharacterIndex = static_cast<int>(i);
                selectedCharacterGuid = character.guid;
                saveLastCharacter(character.guid);
            }

            // Double-click to enter world
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                selectedCharacterIndex = static_cast<int>(i);
                selectedCharacterGuid = character.guid;
                saveLastCharacter(character.guid);
                characterSelected = true;
                gameHandler.selectCharacter(character.guid);
                if (onCharacterSelected) onCharacterSelected(character.guid);
            }
            ImGui::PopID();
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", character.level);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", game::getRaceName(character.race));

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", game::getClassName(character.characterClass));

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", character.zoneId);
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    // ── Right: Details panel ──
    if (detailPanelW > 0.0f &&
        selectedCharacterIndex >= 0 &&
        selectedCharacterIndex < static_cast<int>(characters.size())) {

        const auto& character = characters[selectedCharacterIndex];

        // Keep the 3D preview in sync with the selected character.
        if (assetManager_ && assetManager_->isInitialized()) {
            if (!preview_) {
                preview_ = std::make_unique<rendering::CharacterPreview>();
            }
            if (!previewInitialized_) {
                previewInitialized_ = preview_->initialize(assetManager_);
                if (!previewInitialized_) {
                    LOG_WARNING("CharacterScreen: failed to init CharacterPreview");
                    preview_.reset();
                }
            }
            if (preview_) {
                const uint64_t equipHash = hashEquipment(character.equipment);
                const bool changed =
                    (previewGuid_ != character.guid) ||
                    (previewAppearanceBytes_ != character.appearanceBytes) ||
                    (previewFacialFeatures_ != character.facialFeatures) ||
                    (previewUseFemaleModel_ != character.useFemaleModel) ||
                    (previewEquipHash_ != equipHash);

                if (changed) {
                    uint8_t skin = character.appearanceBytes & 0xFF;
                    uint8_t face = (character.appearanceBytes >> 8) & 0xFF;
                    uint8_t hairStyle = (character.appearanceBytes >> 16) & 0xFF;
                    uint8_t hairColor = (character.appearanceBytes >> 24) & 0xFF;

                    if (preview_->loadCharacter(character.race, character.gender,
                                                skin, face, hairStyle, hairColor,
                                                character.facialFeatures, character.useFemaleModel)) {
                        preview_->applyEquipment(character.equipment);
                    }

                    previewGuid_ = character.guid;
                    previewAppearanceBytes_ = character.appearanceBytes;
                    previewFacialFeatures_ = character.facialFeatures;
                    previewUseFemaleModel_ = character.useFemaleModel;
                    previewEquipHash_ = equipHash;
                }

                // Drive preview animation and render to its FBO.
                preview_->update(ImGui::GetIO().DeltaTime);
                preview_->render();
            }
        }

        ImGui::SameLine();
        ImGui::BeginChild("CharDetails", ImVec2(detailPanelW, listH), true);

        // 3D preview portrait
        if (preview_ && preview_->getTextureId() != 0) {
            float imgW = ImGui::GetContentRegionAvail().x;
            float imgH = imgW * (static_cast<float>(preview_->getHeight()) /
                                 static_cast<float>(preview_->getWidth()));
            // Clamp to avoid taking the entire panel
            float maxH = 320.0f;
            if (imgH > maxH) {
                imgH = maxH;
                imgW = imgH * (static_cast<float>(preview_->getWidth()) /
                               static_cast<float>(preview_->getHeight()));
            }
            ImGui::Image(
                static_cast<ImTextureID>(preview_->getTextureId()),
                ImVec2(imgW, imgH),
                ImVec2(0.0f, 1.0f),  // flip Y for OpenGL
                ImVec2(1.0f, 0.0f));

            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                preview_->rotate(ImGui::GetIO().MouseDelta.x * 0.2f);
            }
            ImGui::Spacing();
        } else if (!assetManager_ || !assetManager_->isInitialized()) {
            ImGui::TextDisabled("Preview unavailable (assets not loaded)");
            ImGui::Spacing();
        }

        ImGui::TextColored(getFactionColor(character.race), "%s", character.name.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Level %d", character.level);
        ImGui::Text("%s", game::getRaceName(character.race));
        ImGui::Text("%s", game::getClassName(character.characterClass));
        ImGui::Text("%s", game::getGenderName(character.gender));
        ImGui::Spacing();
        ImGui::Text("Map %d, Zone %d", character.mapId, character.zoneId);

        if (character.hasGuild()) {
            ImGui::Text("Guild ID: %d", character.guildId);
        } else {
            ImGui::TextDisabled("No Guild");
        }

        if (character.hasPet()) {
            ImGui::Spacing();
            ImGui::Text("Pet Lv%d (Family %d)", character.pet.level, character.pet.family);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Enter World button — full width
        float btnW = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Enter World", ImVec2(btnW, 44))) {
            characterSelected = true;
            saveLastCharacter(character.guid);
            std::stringstream ss;
            ss << "Entering world with " << character.name << "...";
            setStatus(ss.str());
            gameHandler.selectCharacter(character.guid);
            if (onCharacterSelected) onCharacterSelected(character.guid);
        }

        ImGui::EndChild();
    }

    // ── Bottom button row ──
    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(120, 36))) { if (onBack) onBack(); }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(120, 36))) {
        if (gameHandler.getState() == game::WorldState::READY ||
            gameHandler.getState() == game::WorldState::CHAR_LIST_RECEIVED) {
            gameHandler.requestCharacterList();
            setStatus("Refreshing character list...");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Character", ImVec2(160, 36))) {
        if (onCreateCharacter) onCreateCharacter();
    }

    // Delete button — small, red, far right, only when a character is selected
    if (selectedCharacterIndex >= 0 &&
        selectedCharacterIndex < static_cast<int>(characters.size())) {
        float deleteW = 80.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - deleteW);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(deleteW, 28))) {
            deleteConfirmStage = 1;
            ImGui::OpenPopup("DeleteConfirm1");
        }
        ImGui::PopStyleColor(3);
    }

    // First confirmation popup
    if (ImGui::BeginPopupModal("DeleteConfirm1", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        const auto& ch = characters[selectedCharacterIndex];
        ImGui::Text("Are you sure you want to delete");
        ImGui::TextColored(getFactionColor(ch.race), "%s", ch.name.c_str());
        ImGui::Text("Level %d %s %s?",
            ch.level, game::getRaceName(ch.race), game::getClassName(ch.characterClass));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Yes, delete this character", ImVec2(240, 32))) {
            ImGui::CloseCurrentPopup();
            deleteConfirmStage = 2;
            ImGui::OpenPopup("DeleteConfirm2");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 32))) {
            deleteConfirmStage = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Second (final) confirmation popup
    if (deleteConfirmStage == 2) {
        ImGui::OpenPopup("DeleteConfirm2");
    }
    if (ImGui::BeginPopupModal("DeleteConfirm2", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        const auto& ch = characters[selectedCharacterIndex];
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::Text("THIS CANNOT BE UNDONE!");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Text("Are you REALLY sure you want to permanently");
        ImGui::Text("delete %s? This character will be gone forever.", ch.name.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("DELETE PERMANENTLY", ImVec2(240, 32))) {
            if (onDeleteCharacter) onDeleteCharacter(ch.guid);
            deleteConfirmStage = 0;
            selectedCharacterIndex = -1;
            selectedCharacterGuid = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 32))) {
            deleteConfirmStage = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void CharacterScreen::setStatus(const std::string& message) {
    statusMessage = message;
}

void CharacterScreen::selectCharacterByName(const std::string& name) {
    newlyCreatedCharacterName = name;
    restoredLastCharacter = false;  // Allow re-selection in render()
    selectedCharacterIndex = -1;
}

ImVec4 CharacterScreen::getFactionColor(game::Race race) const {
    // Alliance races: blue
    if (race == game::Race::HUMAN ||
        race == game::Race::DWARF ||
        race == game::Race::NIGHT_ELF ||
        race == game::Race::GNOME ||
        race == game::Race::DRAENEI) {
        return ImVec4(0.3f, 0.5f, 1.0f, 1.0f);
    }

    // Horde races: red
    if (race == game::Race::ORC ||
        race == game::Race::UNDEAD ||
        race == game::Race::TAUREN ||
        race == game::Race::TROLL ||
        race == game::Race::BLOOD_ELF) {
        return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    }

    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

std::string CharacterScreen::getConfigDir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return appdata ? std::string(appdata) + "\\wowee" : ".";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.wowee" : ".";
#endif
}

void CharacterScreen::saveLastCharacter(uint64_t guid) {
    std::string dir = getConfigDir();
    std::filesystem::create_directories(dir);
    std::ofstream f(dir + "/last_character.cfg");
    if (f) f << guid;
}

uint64_t CharacterScreen::loadLastCharacter() {
    std::string path = getConfigDir() + "/last_character.cfg";
    std::ifstream f(path);
    uint64_t guid = 0;
    if (f) f >> guid;
    return guid;
}

}} // namespace wowee::ui
