# Architecture Overview

## System Design

Wowee follows a modular architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────┐
│           Application (main loop)            │
│  - State management (auth/realms/game)      │
│  - Update cycle (60 FPS)                    │
│  - Event dispatch                           │
└──────────────┬──────────────────────────────┘
               │
       ┌───────┴────────┐
       │                │
┌──────▼──────┐  ┌─────▼──────┐
│   Window    │  │   Input    │
│  (SDL2)     │  │ (Keyboard/ │
│             │  │   Mouse)   │
└──────┬──────┘  └─────┬──────┘
       │                │
       └───────┬────────┘
               │
    ┌──────────┴──────────┐
    │                     │
┌───▼────────┐   ┌───────▼──────┐
│  Renderer  │   │  UI Manager  │
│ (OpenGL)   │   │   (ImGui)    │
└───┬────────┘   └──────────────┘
    │
    ├─ Camera
    ├─ Scene Graph
    ├─ Shaders
    ├─ Meshes
    └─ Textures
```

## Core Systems

### 1. Application Layer (`src/core/`)

**Application** - Main controller
- Owns all subsystems
- Manages application state
- Runs update/render loop
- Handles lifecycle (init/shutdown)

**Window** - SDL2 wrapper
- Creates window and OpenGL context
- Handles resize events
- Manages VSync and fullscreen

**Input** - Input management
- Keyboard state tracking
- Mouse position and buttons
- Mouse locking for camera control

**Logger** - Logging system
- Thread-safe logging
- Multiple log levels (DEBUG, INFO, WARNING, ERROR, FATAL)
- Timestamp formatting

### 2. Rendering System (`src/rendering/`)

**Renderer** - Main rendering coordinator
- Manages OpenGL state
- Coordinates frame rendering
- Owns camera and scene

**Camera** - View/projection matrices
- Position and orientation
- FOV and aspect ratio
- View frustum (for culling)

**Scene** - Scene graph
- Mesh collection
- Spatial organization
- Visibility determination

**Shader** - GLSL program wrapper
- Loads vertex/fragment shaders
- Uniform management
- Compilation and linking

**Mesh** - Geometry container
- Vertex buffer (position, normal, texcoord)
- Index buffer
- VAO/VBO/EBO management

**Texture** - Texture management
- Loading (will support BLP format)
- OpenGL texture object
- Mipmap generation

**Material** - Surface properties
- Shader assignment
- Texture binding
- Color/properties

### 3. Networking (`src/network/`)

**Socket** (Abstract base class)
- Connection interface
- Packet send/receive
- Callback system

**TCPSocket** - Linux TCP sockets
- Non-blocking I/O
- Raw TCP (replaces WebSocket)
- Packet framing

**Packet** - Binary data container
- Read/write primitives
- Byte order handling
- Opcode management

### 4. Authentication (`src/auth/`)

**AuthHandler** - Auth server protocol
- Connects to port 3724
- SRP authentication flow
- Session key generation

**SRP** - Secure Remote Password
- SRP6a algorithm
- Big integer math
- Salt and verifier generation

**Crypto** - Cryptographic functions
- SHA1 hashing (OpenSSL)
- Random number generation
- Encryption helpers

### 5. Game Logic (`src/game/`)

**GameHandler** - World server protocol
- Connects to port 8085 (configurable)
- Packet handlers for 100+ opcodes
- Session management with RC4 encryption
- Character enumeration and login flow

**World** - Game world state
- Map loading with async terrain streaming
- Entity management (players, NPCs, creatures)
- Zone management and exploration
- Time-of-day synchronization

**Player** - Player character
- Position and movement (WASD + spline movement)
- Stats tracking (health, mana, XP, level)
- Equipment and inventory (23 + 16 slots)
- Action queue and spell casting
- Death and resurrection handling

**Character** - Character data
- Race, class, gender, appearance
- Creation and customization
- 3D model preview
- Online character lifecycle and state synchronization

**Entity** - Game entities
- NPCs and creatures with display info
- Animation state (idle, combat, walk, run)
- GUID management (player, creature, item, gameobject)
- Targeting and selection

**Inventory** - Item management
- Equipment slots (head, shoulders, chest, etc.)
- Backpack storage (16 slots)
- Item metadata (icons, stats, durability)
- Drag-drop system
- Auto-equip and unequip

**NPC Interactions** - handled through `GameHandler`
- Gossip system
- Quest givers with markers (! and ?)
- Vendors (buy/sell)
- Trainers (placeholder)
- Combat animations

**ZoneManager** - Zone and area tracking
- Map exploration
- Area discovery
- Zone change detection

**Opcodes** - Protocol definitions
- 100+ Client→Server opcodes (CMSG_*)
- 100+ Server→Client opcodes (SMSG_*)
- WoW 3.3.5a (build 12340) specific

### 6. Asset Pipeline (`src/pipeline/`)

**MPQManager** - Archive management
- Loads .mpq files (via StormLib)
- Priority-based file lookup (patch files override base files)
- Data extraction with caching
- Locale support (enUS, enGB, etc.)

**BLPLoader** - Texture parser
- BLP format (Blizzard texture format)
- DXT1/3/5 compression support
- Mipmap extraction and generation
- OpenGL texture object creation

**M2Loader** - Model parser
- Character/creature models with materials
- Skeletal animation data (256 bones max)
- Bone hierarchies and transforms
- Animation sequences (idle, walk, run, attack, etc.)
- Particle emitters (WotLK FBlock format)
- Attachment points (weapons, mounts, etc.)
- Geoset support (hide/show body parts)
- Multiple texture units and render batches

**WMOLoader** - World object parser
- Buildings and structures
- Multi-material batches
- Portal system (visibility culling)
- Doodad placement (decorations)
- Group-based rendering
- Liquid data (indoor water)

**ADTLoader** - Terrain parser
- 64x64 tiles per map (map_XX_YY.adt)
- 16x16 chunks per tile (MCNK)
- Height map data (9x9 outer + 8x8 inner vertices)
- Texture layers (up to 4 per chunk with alpha blending)
- Liquid data (water/lava/slime with height and flags)
- Object placement (M2 and WMO references)
- Terrain holes
- Async loading to prevent frame stalls

**DBCLoader** - Database parser
- 20+ DBC files loaded (Spell, Item, Creature, SkillLine, Faction, etc.)
- Type-safe record access
- String block parsing
- Memory-efficient caching
- Used for:
  - Spell icons and tooltips (Spell.dbc, SpellIcon.dbc)
  - Item data (Item.dbc, ItemDisplayInfo.dbc)
  - Creature display info (CreatureDisplayInfo.dbc, CreatureModelData.dbc)
  - Class and race info (ChrClasses.dbc, ChrRaces.dbc)
  - Skill lines (SkillLine.dbc, SkillLineAbility.dbc)
  - Faction and reputation (Faction.dbc)
  - Map and area names (Map.dbc, AreaTable.dbc)

### 7. UI System (`src/ui/`)

**UIManager** - ImGui coordinator
- ImGui initialization with SDL2/OpenGL backend
- Event handling and input routing
- Render dispatch with opacity control
- Screen state management

**AuthScreen** - Login interface
- Username/password input fields
- Server address configuration
- Connection status and error messages

**RealmScreen** - Server selection
- Realm list display with names and types
- Population info (Low/Medium/High/Full)
- Realm type indicators (PvP/PvE/RP/RPPvP)
- Auto-select for single realm

**CharacterScreen** - Character selection
- Character list with 3D animated preview
- Stats panel (level, race, class, location)
- Create/delete character buttons
- Enter world button
- Auto-select for single character

**CharacterCreateScreen** - Character creation
- Race selection (all Alliance and Horde races)
- Class selection (class availability by race)
- Gender selection
- Appearance customization (face, skin, hair, color, features)
- Name input with validation
- 3D character preview

**GameScreen** - In-game HUD
- Chat window with message history and formatting
- Action bar (12 slots with icons, cooldowns, keybindings)
- Target frame (name, level, health, hostile/friendly coloring)
- Player stats (health, mana/rage/energy)
- Minimap with quest markers
- Experience bar

**InventoryScreen** - Inventory management
- Equipment paper doll (23 slots: head, shoulders, chest, etc.)
- Backpack grid (16 slots)
- Item icons with tooltips
- Drag-drop to equip/unequip
- Item stats and durability
- Gold display

**SpellbookScreen** - Spells and abilities
- Tabbed interface (class specialties + General)
- Spell icons organized by SkillLine
- Spell tooltips (name, rank, cost, cooldown, description)
- Drag-drop to action bar
- Known spell tracking

**QuestLogScreen** - Quest tracking
- Active quest list
- Quest objectives and progress
- Quest details (description, objectives, rewards)
- Abandon quest button
- Quest level and recommended party size

**TalentScreen** - Talent trees
- Placeholder for talent system
- Tree visualization (TODO)
- Talent point allocation (TODO)

**Settings Window** - Configuration
- UI opacity slider
- Graphics options (TODO)
- Audio controls (TODO)
- Keybinding customization (TODO)

**Loading Screen** - Map loading progress
- Progress bar with percentage
- Background image (map-specific, TODO)
- Loading tips (TODO)
- Shown during world entry and map transitions

## Data Flow Examples

### Authentication Flow
```
User Input (username/password)
    ↓
AuthHandler::authenticate()
    ↓
SRP::calculateVerifier()
    ↓
TCPSocket::send(LOGON_CHALLENGE)
    ↓
Server Response (LOGON_CHALLENGE)
    ↓
AuthHandler receives packet
    ↓
SRP::calculateProof()
    ↓
TCPSocket::send(LOGON_PROOF)
    ↓
Server Response (LOGON_PROOF) → Success
    ↓
Application::setState(REALM_SELECTION)
```

### Rendering Flow
```
Application::render()
    ↓
Renderer::beginFrame()
    ├─ glClearColor() - Clear screen
    └─ glClear() - Clear buffers
    ↓
Renderer::renderWorld(world)
    ├─ Update camera matrices
    ├─ Frustum culling
    ├─ For each visible chunk:
    │   ├─ Bind shader
    │   ├─ Set uniforms (matrices, lighting)
    │   ├─ Bind textures
    │   └─ Mesh::draw() → glDrawElements()
    └─ For each entity:
        ├─ Calculate bone transforms
        └─ Render skinned mesh
    ↓
UIManager::render()
    ├─ ImGui::NewFrame()
    ├─ Render current UI screen
    └─ ImGui::Render()
    ↓
Renderer::endFrame()
    ↓
Window::swapBuffers()
```

### Asset Loading Flow
```
World::loadMap(mapId)
    ↓
MPQManager::readFile("World/Maps/{map}/map.adt")
    ↓
ADTLoader::load(adtData)
    ├─ Parse MCNK chunks (terrain)
    ├─ Parse MCLY chunks (textures)
    ├─ Parse MCVT chunks (vertices)
    └─ Parse MCNR chunks (normals)
    ↓
For each texture reference:
    MPQManager::readFile(texturePath)
    ↓
    BLPLoader::load(blpData)
    ↓
    Texture::loadFromMemory(imageData)
    ↓
Create Mesh from vertices/normals/texcoords
    ↓
Add to Scene
    ↓
Renderer draws in next frame
```

## Threading Model

Currently **single-threaded** with async operations:
- Main thread: Window events, update, render
- Network I/O: Non-blocking in main thread (event-driven)
- Asset loading: Async terrain streaming (non-blocking chunk loads)

**Async Systems Implemented:**
- Terrain streaming loads ADT chunks asynchronously to prevent frame stalls
- Network packets processed in batches per frame
- UI rendering deferred until after world rendering

**Future multi-threading opportunities:**
- Asset loading thread pool (background texture/model decompression)
- Network thread (dedicated for socket I/O)
- Physics thread (if collision detection is added)
- Audio streaming thread

## Memory Management

- **Smart pointers:** Used throughout (std::unique_ptr, std::shared_ptr)
- **RAII:** All resources (OpenGL, SDL) cleaned up automatically
- **No manual memory management:** No raw new/delete
- **OpenGL resources:** Wrapped in classes with proper destructors

## Performance Considerations

### Rendering
- **Frustum culling:** Only render visible chunks (terrain and WMO groups)
- **Distance culling:** WMO groups culled beyond 160 units
- **Batching:** Group draw calls by material and shader
- **LOD:** Distance-based level of detail (TODO)
- **Occlusion:** Portal-based visibility (WMO system)
- **GPU skinning:** Character animation computed on GPU (256 bones)
- **Instancing:** Future optimization for repeated models

### Asset Streaming
- **Async loading:** Terrain chunks load asynchronously (prevents frame stalls)
- **Lazy loading:** Load chunks as player moves within streaming radius
- **Unloading:** Free distant chunks automatically
- **Caching:** Keep frequently used assets in memory (textures, models)
- **Priority queue:** Load visible chunks first

### Network
- **Non-blocking I/O:** Never stall main thread
- **Packet buffering:** Handle multiple packets per frame
- **Batch processing:** Process received packets in batches
- **RC4 encryption:** Efficient header encryption (minimal overhead)
- **Compression:** Some packets are compressed (TODO)

### Memory Management
- **Smart pointers:** Automatic cleanup, no memory leaks
- **Object pooling:** Reuse particle objects (weather system)
- **DBC caching:** Load once, access fast
- **Texture sharing:** Same texture used by multiple models

## Error Handling

- **Logging:** All errors logged with context
- **Graceful degradation:** Missing assets show placeholder
- **State recovery:** Network disconnect → back to auth screen
- **No crashes:** Exceptions caught at application level

## Configuration

Currently hardcoded, future config system:
- Window size and fullscreen
- Graphics quality settings
- Server addresses
- Keybindings
- Audio volume

## Testing Strategy

**Unit Testing** (TODO):
- Packet serialization/deserialization
- SRP math functions
- Asset parsers with sample files
- DBC record parsing
- Inventory slot calculations

**Integration Testing** (TODO):
- Full auth flow against test server
- Realm list retrieval
- Character creation and selection
- Quest turn-in flow
- Vendor transactions

**Manual Testing:**
- Visual verification of rendering (terrain, water, models, particles)
- Performance profiling (F1 performance HUD)
- Memory leak checking (valgrind)
- Online gameplay against AzerothCore/TrinityCore/MaNGOS servers
- UI interactions (drag-drop, click events)

**Current Test Coverage:**
- Full authentication flow tested against live servers
- Character creation and selection verified
- Quest system tested (accept, track, turn-in)
- Vendor system tested (buy, sell)
- Combat system tested (targeting, auto-attack, spells)
- Inventory system tested (equip, unequip, drag-drop)

## Build System

**CMake:**
- Modular target structure
- Automatic dependency discovery
- Cross-platform (Linux focus, but portable)
- Out-of-source builds

**Dependencies:**
- SDL2 (system)
- OpenGL/GLEW (system)
- OpenSSL (system)
- GLM (system or header-only)
- ImGui (submodule in extern/)
- StormLib (system, optional)

## Code Style

- **C++20 standard**
- **Namespaces:** wowee::core, wowee::rendering, etc.
- **Naming:** PascalCase for classes, camelCase for functions/variables
- **Headers:** .hpp extension
- **Includes:** Relative to project root

---

This architecture provides a solid foundation for a full-featured native WoW client!
