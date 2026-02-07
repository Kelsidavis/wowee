#include "ui/spellbook_screen.hpp"
#include "core/input.hpp"
#include "core/application.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/blp_loader.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <map>

namespace wowee { namespace ui {

// General utility spells that belong in the General tab
static bool isGeneralSpell(uint32_t spellId) {
    switch (spellId) {
        case 6603:  // Attack
        case 8690:  // Hearthstone
        case 3365:  // Opening
        case 21651: // Opening
        case 21652: // Closing
            return true;
        default:
            return false;
    }
}

void SpellbookScreen::loadSpellDBC(pipeline::AssetManager* assetManager) {
    if (dbcLoadAttempted) return;
    dbcLoadAttempted = true;

    if (!assetManager || !assetManager->isInitialized()) return;

    auto dbc = assetManager->loadDBC("Spell.dbc");
    if (!dbc || !dbc->isLoaded()) {
        LOG_WARNING("Spellbook: Could not load Spell.dbc");
        return;
    }

    uint32_t fieldCount = dbc->getFieldCount();
    if (fieldCount < 154) {
        LOG_WARNING("Spellbook: Spell.dbc has ", fieldCount, " fields, expected 234+");
        return;
    }

    // WoW 3.3.5a Spell.dbc fields (0-based):
    // 0 = SpellID, 4 = Attributes, 133 = SpellIconID, 136 = SpellName_enUS, 153 = RankText_enUS
    uint32_t count = dbc->getRecordCount();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t spellId = dbc->getUInt32(i, 0);
        if (spellId == 0) continue;

        SpellInfo info;
        info.spellId = spellId;
        info.attributes = dbc->getUInt32(i, 4);
        info.iconId = dbc->getUInt32(i, 133);
        info.name = dbc->getString(i, 136);
        info.rank = dbc->getString(i, 153);

        if (!info.name.empty()) {
            spellData[spellId] = std::move(info);
        }
    }

    dbcLoaded = true;
    LOG_INFO("Spellbook: Loaded ", spellData.size(), " spells from Spell.dbc");
}

void SpellbookScreen::loadSpellIconDBC(pipeline::AssetManager* assetManager) {
    if (iconDbLoaded) return;
    iconDbLoaded = true;

    if (!assetManager || !assetManager->isInitialized()) return;

    auto dbc = assetManager->loadDBC("SpellIcon.dbc");
    if (!dbc || !dbc->isLoaded()) {
        LOG_WARNING("Spellbook: Could not load SpellIcon.dbc");
        return;
    }

    for (uint32_t i = 0; i < dbc->getRecordCount(); i++) {
        uint32_t id = dbc->getUInt32(i, 0);
        std::string path = dbc->getString(i, 1);
        if (!path.empty() && id > 0) {
            spellIconPaths[id] = path;
        }
    }

    LOG_INFO("Spellbook: Loaded ", spellIconPaths.size(), " spell icon paths");
}

void SpellbookScreen::loadSkillLineDBCs(pipeline::AssetManager* assetManager) {
    if (skillLineDbLoaded) return;
    skillLineDbLoaded = true;

    if (!assetManager || !assetManager->isInitialized()) return;

    // Load SkillLine.dbc: field 0 = ID, field 1 = categoryID, field 3 = name_enUS
    auto skillLineDbc = assetManager->loadDBC("SkillLine.dbc");
    if (skillLineDbc && skillLineDbc->isLoaded()) {
        for (uint32_t i = 0; i < skillLineDbc->getRecordCount(); i++) {
            uint32_t id = skillLineDbc->getUInt32(i, 0);
            std::string name = skillLineDbc->getString(i, 3);
            if (id > 0 && !name.empty()) {
                skillLineNames[id] = name;
            }
        }
        LOG_INFO("Spellbook: Loaded ", skillLineNames.size(), " skill lines");
    } else {
        LOG_WARNING("Spellbook: Could not load SkillLine.dbc");
    }

    // Load SkillLineAbility.dbc: field 0 = ID, field 1 = skillLineID, field 2 = spellID
    auto slaDbc = assetManager->loadDBC("SkillLineAbility.dbc");
    if (slaDbc && slaDbc->isLoaded()) {
        for (uint32_t i = 0; i < slaDbc->getRecordCount(); i++) {
            uint32_t skillLineId = slaDbc->getUInt32(i, 1);
            uint32_t spellId = slaDbc->getUInt32(i, 2);
            if (spellId > 0 && skillLineId > 0) {
                spellToSkillLine[spellId] = skillLineId;
            }
        }
        LOG_INFO("Spellbook: Loaded ", spellToSkillLine.size(), " skill line abilities");
    } else {
        LOG_WARNING("Spellbook: Could not load SkillLineAbility.dbc");
    }
}

void SpellbookScreen::categorizeSpells(const std::vector<uint32_t>& knownSpells) {
    spellTabs.clear();

    // Group spells by skill line, preserving order of first appearance
    std::map<uint32_t, std::vector<const SpellInfo*>> skillLineSpells;
    std::vector<const SpellInfo*> generalSpells;

    for (uint32_t spellId : knownSpells) {
        auto it = spellData.find(spellId);
        if (it == spellData.end()) continue;

        const SpellInfo* info = &it->second;

        if (isGeneralSpell(spellId)) {
            generalSpells.push_back(info);
            continue;
        }

        auto slIt = spellToSkillLine.find(spellId);
        if (slIt != spellToSkillLine.end()) {
            skillLineSpells[slIt->second].push_back(info);
        } else {
            generalSpells.push_back(info);
        }
    }

    auto byName = [](const SpellInfo* a, const SpellInfo* b) { return a->name < b->name; };

    // General tab first
    if (!generalSpells.empty()) {
        std::sort(generalSpells.begin(), generalSpells.end(), byName);
        spellTabs.push_back({"General", std::move(generalSpells)});
    }

    // Skill line tabs sorted by name
    std::vector<std::pair<std::string, std::vector<const SpellInfo*>>> named;
    for (auto& [skillLineId, spells] : skillLineSpells) {
        auto nameIt = skillLineNames.find(skillLineId);
        std::string tabName = (nameIt != skillLineNames.end()) ? nameIt->second
                              : "Unknown (" + std::to_string(skillLineId) + ")";
        std::sort(spells.begin(), spells.end(), byName);
        named.push_back({std::move(tabName), std::move(spells)});
    }
    std::sort(named.begin(), named.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [name, spells] : named) {
        spellTabs.push_back({std::move(name), std::move(spells)});
    }

    lastKnownSpellCount = knownSpells.size();
}

GLuint SpellbookScreen::getSpellIcon(uint32_t iconId, pipeline::AssetManager* assetManager) {
    if (iconId == 0 || !assetManager) return 0;

    auto cit = spellIconCache.find(iconId);
    if (cit != spellIconCache.end()) return cit->second;

    auto pit = spellIconPaths.find(iconId);
    if (pit == spellIconPaths.end()) {
        spellIconCache[iconId] = 0;
        return 0;
    }

    std::string iconPath = pit->second + ".blp";
    auto blpData = assetManager->readFile(iconPath);
    if (blpData.empty()) {
        spellIconCache[iconId] = 0;
        return 0;
    }

    auto image = pipeline::BLPLoader::load(blpData);
    if (!image.isValid()) {
        spellIconCache[iconId] = 0;
        return 0;
    }

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    spellIconCache[iconId] = texId;
    return texId;
}

const SpellInfo* SpellbookScreen::getSpellInfo(uint32_t spellId) const {
    auto it = spellData.find(spellId);
    return (it != spellData.end()) ? &it->second : nullptr;
}

void SpellbookScreen::render(game::GameHandler& gameHandler, pipeline::AssetManager* assetManager) {
    // P key toggle (edge-triggered)
    bool uiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    bool pDown = !uiWantsKeyboard && core::Input::getInstance().isKeyPressed(SDL_SCANCODE_P);
    if (pDown && !pKeyWasDown) {
        open = !open;
    }
    pKeyWasDown = pDown;

    if (!open) return;

    // Lazy-load DBC data on first open
    if (!dbcLoadAttempted) {
        loadSpellDBC(assetManager);
        loadSpellIconDBC(assetManager);
        loadSkillLineDBCs(assetManager);
    }

    // Rebuild categories if spell list changed
    const auto& spells = gameHandler.getKnownSpells();
    if (spells.size() != lastKnownSpellCount) {
        categorizeSpells(spells);
    }

    auto* window = core::Application::getInstance().getWindow();
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) : 720.0f;

    float bookW = 360.0f;
    float bookH = std::min(520.0f, screenH - 120.0f);
    float bookX = screenW - bookW - 10.0f;
    float bookY = 80.0f;

    ImGui::SetNextWindowPos(ImVec2(bookX, bookY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(bookW, bookH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(screenW, screenH));

    bool windowOpen = open;
    if (ImGui::Begin("Spellbook", &windowOpen)) {
        // Clamp window position to stay on screen
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        float clampedX = std::max(0.0f, std::min(winPos.x, screenW - winSize.x));
        float clampedY = std::max(0.0f, std::min(winPos.y, screenH - winSize.y));
        if (clampedX != winPos.x || clampedY != winPos.y) {
            ImGui::SetWindowPos(ImVec2(clampedX, clampedY));
        }

        // Tab bar
        if (ImGui::BeginTabBar("SpellbookTabs")) {
            for (size_t tabIdx = 0; tabIdx < spellTabs.size(); tabIdx++) {
                const auto& tab = spellTabs[tabIdx];

                char tabLabel[64];
                snprintf(tabLabel, sizeof(tabLabel), "%s (%zu)",
                    tab.name.c_str(), tab.spells.size());

                if (ImGui::BeginTabItem(tabLabel)) {
                    if (tab.spells.empty()) {
                        ImGui::TextDisabled("No spells in this category.");
                    }

                    ImGui::BeginChild("SpellList", ImVec2(0, 0), true);

                    float iconSize = 32.0f;

                    for (const SpellInfo* info : tab.spells) {
                        ImGui::PushID(static_cast<int>(info->spellId));

                        float cd = gameHandler.getSpellCooldown(info->spellId);
                        bool onCooldown = cd > 0.0f;
                        bool isPassive = info->isPassive();
                        bool isDim = isPassive || onCooldown;

                        GLuint iconTex = getSpellIcon(info->iconId, assetManager);

                        // Selectable consumes clicks properly (prevents window drag)
                        ImGui::Selectable("##row", false,
                            ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, iconSize));
                        bool rowHovered = ImGui::IsItemHovered();
                        bool rowClicked = ImGui::IsItemClicked(0);
                        ImVec2 rMin = ImGui::GetItemRectMin();
                        auto* dl = ImGui::GetWindowDrawList();

                        // Draw icon on top of selectable
                        if (iconTex) {
                            dl->AddImage((ImTextureID)(uintptr_t)iconTex,
                                rMin, ImVec2(rMin.x + iconSize, rMin.y + iconSize));
                        } else {
                            dl->AddRectFilled(rMin,
                                ImVec2(rMin.x + iconSize, rMin.y + iconSize),
                                IM_COL32(60, 60, 80, 255));
                        }

                        // Draw name and rank text
                        ImU32 textCol = isDim ? IM_COL32(153, 153, 153, 255)
                                              : ImGui::GetColorU32(ImGuiCol_Text);
                        ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        float textX = rMin.x + iconSize + 4.0f;
                        dl->AddText(ImVec2(textX, rMin.y), textCol, info->name.c_str());
                        if (!info->rank.empty()) {
                            dl->AddText(ImVec2(textX, rMin.y + ImGui::GetTextLineHeight()),
                                dimCol, info->rank.c_str());
                        } else if (onCooldown) {
                            char cdBuf[32];
                            snprintf(cdBuf, sizeof(cdBuf), "%.1fs cooldown", cd);
                            dl->AddText(ImVec2(textX, rMin.y + ImGui::GetTextLineHeight()),
                                dimCol, cdBuf);
                        }

                        if (rowHovered) {
                            // Start drag on click (not passive)
                            if (rowClicked && !isPassive) {
                                draggingSpell_ = true;
                                dragSpellId_ = info->spellId;
                                dragSpellIconTex_ = iconTex;
                            }

                            if (ImGui::IsMouseDoubleClicked(0) && !isPassive && !onCooldown) {
                                draggingSpell_ = false;
                                dragSpellId_ = 0;
                                dragSpellIconTex_ = 0;
                                uint64_t target = gameHandler.hasTarget() ? gameHandler.getTargetGuid() : 0;
                                gameHandler.castSpell(info->spellId, target);
                            }

                            // Tooltip (only when not dragging)
                            if (!draggingSpell_) {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s", info->name.c_str());
                                if (!info->rank.empty()) {
                                    ImGui::TextDisabled("%s", info->rank.c_str());
                                }
                                ImGui::TextDisabled("Spell ID: %u", info->spellId);
                                if (isPassive) {
                                    ImGui::TextDisabled("Passive");
                                } else {
                                    ImGui::TextDisabled("Drag to action bar to assign");
                                    if (!onCooldown) {
                                        ImGui::TextDisabled("Double-click to cast");
                                    }
                                }
                                ImGui::EndTooltip();
                            }
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if (!windowOpen) {
        open = false;
    }

    // Render dragged spell icon at cursor
    if (draggingSpell_ && dragSpellId_ != 0) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float dragSize = 32.0f;
        if (dragSpellIconTex_) {
            ImGui::GetForegroundDrawList()->AddImage(
                (ImTextureID)(uintptr_t)dragSpellIconTex_,
                ImVec2(mousePos.x - dragSize * 0.5f, mousePos.y - dragSize * 0.5f),
                ImVec2(mousePos.x + dragSize * 0.5f, mousePos.y + dragSize * 0.5f));
        } else {
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(mousePos.x - dragSize * 0.5f, mousePos.y - dragSize * 0.5f),
                ImVec2(mousePos.x + dragSize * 0.5f, mousePos.y + dragSize * 0.5f),
                IM_COL32(80, 80, 120, 180));
        }

        // Cancel drag on mouse release (action bar consumes it before this if dropped on a slot)
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            draggingSpell_ = false;
            dragSpellId_ = 0;
            dragSpellIconTex_ = 0;
        }
    }
}

}} // namespace wowee::ui
