# Quick Start Guide

## Current Status

Wowee is a native C++ World of Warcraft 3.3.5a client focused on online multiplayer.

Implemented today:

- SRP6a authentication + world connection
- Character creation/selection and in-world entry
- Full 3D rendering pipeline (terrain, water, sky, M2/WMO, particles)
- Core gameplay systems (movement, combat, spells, inventory, quests, vendors, loot, chat)
- Transport support (boats/zeppelins) with active ongoing fixes

## Build And Run

### 1. Clone

```bash
git clone https://github.com/Kelsidavis/WoWee.git
cd wowee
```

### 2. Install ImGui

```bash
git clone https://github.com/ocornut/imgui.git extern/imgui
```

### 3. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### 4. Provide WoW Data

Put your legal WoW 3.3.5a data in `Data/` (or set `WOW_DATA_PATH`).

### 5. Run

```bash
./build/bin/wowee
```

## Connect To A Server

1. Launch `./build/bin/wowee`
2. Enter account credentials
3. Set auth server address (default: `localhost`)
4. Login, pick realm, pick character, enter world

For local AzerothCore setup, see `docs/server-setup.md`.

## Useful Controls

- `WASD`: Move
- `Mouse`: Look/orbit camera
- `Tab`: Cycle targets
- `1-9,0,-,=`: Action bar slots
- `I`: Inventory
- `P`: Spellbook
- `L`: Quest log
- `Enter`: Chat
- `F1`: Performance HUD
- `F2`: Wireframe

## Troubleshooting

### Build fails on missing dependencies

Use `BUILD_INSTRUCTIONS.md` for distro-specific package lists.

### Client cannot connect

- Verify auth/world server is running
- Check host/port settings
- Check server logs and client logs in `build/bin/logs/`

### Missing assets (models/textures/terrain)

- Verify WoW data files exist under `Data/`
- Or export `WOW_DATA_PATH=/path/to/WoW/Data`
