#pragma once

#include "game/game_handler.hpp"
#include <GL/glew.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

namespace wowee {

namespace pipeline { class AssetManager; }

namespace ui {

struct SpellInfo {
    uint32_t spellId = 0;
    std::string name;
    std::string rank;
    uint32_t iconId = 0;       // SpellIconID
    uint32_t attributes = 0;   // Spell attributes (field 75)
    bool isPassive() const { return (attributes & 0x40) != 0; }
};

struct SpellTabInfo {
    std::string name;
    std::vector<const SpellInfo*> spells;
};

class SpellbookScreen {
public:
    void render(game::GameHandler& gameHandler, pipeline::AssetManager* assetManager);
    bool isOpen() const { return open; }
    void toggle() { open = !open; }
    void setOpen(bool o) { open = o; }

    // Drag-and-drop state for action bar assignment
    bool isDraggingSpell() const { return draggingSpell_; }
    uint32_t getDragSpellId() const { return dragSpellId_; }
    void consumeDragSpell() { draggingSpell_ = false; dragSpellId_ = 0; dragSpellIconTex_ = 0; }

private:
    bool open = false;
    bool pKeyWasDown = false;

    // Spell data (loaded from Spell.dbc)
    bool dbcLoaded = false;
    bool dbcLoadAttempted = false;
    std::unordered_map<uint32_t, SpellInfo> spellData;

    // Icon data (loaded from SpellIcon.dbc)
    bool iconDbLoaded = false;
    std::unordered_map<uint32_t, std::string> spellIconPaths; // SpellIconID -> path
    std::unordered_map<uint32_t, GLuint> spellIconCache;      // SpellIconID -> GL texture

    // Skill line data (loaded from SkillLine.dbc + SkillLineAbility.dbc)
    bool skillLineDbLoaded = false;
    std::unordered_map<uint32_t, std::string> skillLineNames;    // skillLineID -> name
    std::unordered_map<uint32_t, uint32_t> spellToSkillLine;     // spellID -> skillLineID

    // Categorized spell tabs (rebuilt when spell list changes)
    // ordered map so tabs appear in consistent order
    std::vector<SpellTabInfo> spellTabs;
    size_t lastKnownSpellCount = 0;

    // Drag-and-drop from spellbook to action bar
    bool draggingSpell_ = false;
    uint32_t dragSpellId_ = 0;
    GLuint dragSpellIconTex_ = 0;

    void loadSpellDBC(pipeline::AssetManager* assetManager);
    void loadSpellIconDBC(pipeline::AssetManager* assetManager);
    void loadSkillLineDBCs(pipeline::AssetManager* assetManager);
    void categorizeSpells(const std::vector<uint32_t>& knownSpells);
    GLuint getSpellIcon(uint32_t iconId, pipeline::AssetManager* assetManager);
    const SpellInfo* getSpellInfo(uint32_t spellId) const;
};

} // namespace ui
} // namespace wowee
