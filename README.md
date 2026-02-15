# DAD Research Tool

A real-time Unreal Engine 5 memory analysis and visualization tool for **Dark and Darker**, built for reverse engineering research and educational purposes.

Reads game state from memory (actors, positions, health, items) and renders an informational overlay using a transparent DirectX 11 window. **Strictly read-only** — no memory writes, no input injection, no gameplay automation.

> **Disclaimer:** This project is for educational and reverse engineering research only. Use at your own risk. The author is not responsible for any consequences of using this software.

---

## Gallery

<!-- Add your screenshots here. Place images in a docs/screenshots/ folder and reference them: -->

| ESP Overlay | Menu / Settings |
|:-----------:|:---------------:|
| ![ESP Overview](docs/screenshots/esp_overview.png) | ![Menu](docs/screenshots/menu.png) |

| Radar Minimap | Loot Filter |
|:-------------:|:-----------:|
| ![Radar](docs/screenshots/radar.png) | ![Loot](docs/screenshots/loot_filter.png) |

<!-- Single wide screenshot: -->
<!-- ![Full Overlay](docs/screenshots/full_overlay.png) -->

---

## Features

### Entity ESP
- **Players** — bounding boxes (2D / filled / corner styles), name, class, health bar, distance
- **NPCs / Monsters** — same visualization with configurable color, grade filter (Common / Elite / Nightmare)
- **Loot Items** — rarity-colored dots and labels with min-rarity filter (Uncommon through Unique)
- **Chests** — normal (white) and special/golden chests highlighted
- **Portals** — blue markers for escape and dungeon portals

### Minimap Radar
- Top-left radar showing nearby entities relative to camera direction
- Color-coded dots (red = players, yellow = NPCs, green = loot)
- 50m range with distance ring labels
- Self-exclusion (local player filtered out)

### Smart Rendering
- **Position extrapolation** — predicts entity movement 1 frame ahead to eliminate visual lag
- **Bone-accurate boxes** — reads skeletal mesh head/feet bones for pixel-perfect bounding boxes
- **Bone index caching** — scans 7 candidate bones once, caches the best, reuses every frame
- **Sticky death state** — prevents ESP flickering on dead players (20% HP revive threshold)
- **DPI auto-scaling** — automatic scaling for 1080p / 1440p / 4K with configurable font size

### Performance
- **144 FPS frame limiter** — hybrid sleep + spin-wait prevents GPU starvation
- **Two-thread architecture** — heavy actor scanning (~2 Hz background) + smooth rendering (main thread)
- **Optimized text shadows** — 2 draw calls per label instead of 5
- **Cached pointers** — root component, mesh component, attribute set all cached per entity

### Configuration
- JSON-based config file with hot-reload from launcher
- Color pickers for player / team / NPC ESP
- Adjustable render distance (separate sliders for players vs. everything else)
- Rarity filter (Uncommon through Unique)
- Monster grade filter (All / Common+ / Elite+ / Nightmare)
- Box style selector, health bars, distance labels, snap lines

---

## Architecture

```
dad-research-tool/
├── src/
│   ├── core/           # Process memory reader (read-only), config, pattern scanner
│   │   ├── process.*       # ReadProcessMemory wrapper — NO write methods
│   │   ├── config.*        # JSON settings load/save
│   │   ├── integrity.*     # SHA-256 source file tamper detection
│   │   ├── manual_mapper.* # DLL injector for SDK dumping (Dumper-7)
│   │   └── syscalls.h      # Direct syscall stubs for anti-cheat bypass
│   │
│   ├── sdk/            # UE5 engine structure readers
│   │   ├── gnames.*        # FName string table
│   │   ├── gworld.*        # UWorld → ULevel → actor hierarchy
│   │   └── ue5_types.h     # Engine struct definitions (FVector, FTransform, etc.)
│   │
│   ├── game/           # Game-specific entity logic
│   │   ├── actor_manager.* # Actor scanning, classification, health/position reads
│   │   ├── entity.h        # GameEntity struct (position, health, rarity, bones)
│   │   └── item_database.* # Item name → rarity/category mapping
│   │
│   ├── render/         # DirectX 11 overlay rendering
│   │   ├── overlay.*       # Transparent overlay window + D3D11 setup
│   │   ├── drawing.*       # ESP boxes, labels, health bars, radar
│   │   ├── world_to_screen.* # 3D→2D projection (camera + FOV)
│   │   ├── menu.*          # ImGui settings menu
│   │   └── imgui_manager.* # ImGui initialization with DPI scaling
│   │
│   ├── launcher/       # Standalone launcher GUI (dad-launcher.exe)
│   │   ├── panels/         # Settings, injector, log, status panels
│   │   └── util/           # DLL injector, process launcher helpers
│   │
│   ├── proxy/          # version.dll proxy for in-process memory relay
│   └── main.cpp        # Entry point, render loop, frame limiter
│
├── config/             # Runtime configuration
│   ├── settings.example.json   # Template (committed)
│   └── items.json              # Item database
│
├── tools/              # External tools
│   ├── inject.cpp          # CLI DLL injector
│   ├── generate_hashes.py  # Integrity hash generator
│   └── gspots/             # Offset scanner (third-party)
│
├── external/           # Vendored dependencies
│   └── imgui/              # Dear ImGui (docking branch)
│
└── resources/          # Icons, .rc files
```

### Build Targets

| Target | Output | Description |
|--------|--------|-------------|
| `dad-research-tool` | `dad-research-tool.exe` | Main overlay — attaches to game, renders ESP |
| `dad-launcher` | `dad-launcher.exe` | Dashboard GUI — settings editor, injector, log viewer |
| `inject` | `inject.exe` | CLI DLL injector (manual mapping + LoadLibrary fallback) |
| `version-proxy` | `version.dll` | DLL search-order proxy for in-process memory relay |

---

## Building

### Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2022 Build Tools** (or full VS 2022) with C++ desktop workload
- **CMake 3.25+**
- **vcpkg** — C++ package manager

### Setup

```bash
# 1. Clone with submodules
git clone --recurse-submodules https://github.com/yourusername/dad-research-tool.git
cd dad-research-tool

# 2. Install vcpkg dependencies
vcpkg install spdlog nlohmann-json --triplet x64-windows

# 3. Create config from template
copy config\settings.example.json config\settings.json

# 4. Configure CMake (adjust VCPKG_ROOT to your vcpkg path)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

# 5. Build
cmake --build build --config Release
```

### Output

Binaries are in `build/Release/`. Run `dad-research-tool.exe` while Dark and Darker is running in **Borderless Windowed** mode.

### Getting Offsets

The tool needs game-specific memory offsets (GWorld, GNames) that change with each game update:

1. Run **GSpots** (`tools/gspots/GSpots.exe`) against the running game
2. Copy the GWorld and GNames offsets into `config/settings.json`
3. Alternatively, use the **Dumper-7** DLL injector to dump the full SDK

---

## Integrity Verification

Critical source files are SHA-256 hashed to detect tampering. This prevents the tool from being silently modified to add cheat capabilities (memory writes, aimbots, input injection).

```bash
# Verify integrity (run from repo root)
python tools/generate_hashes.py

# If you modify a critical file legitimately, regenerate hashes:
python tools/generate_hashes.py --generate
```

**Protected files** (must remain read-only / visualization-only):
- `process.h/cpp` — memory reader with no write methods
- `main.cpp` — render loop with no input injection
- `world_to_screen.*` — projection math with no aim calculation
- `drawing.*` — ESP rendering with no crosshair/aim overlay
- `overlay.*` — window management with no input hooks
- `entity.h` — data struct with no write-back fields

At runtime, the tool verifies these hashes on startup and refuses to run if tampering is detected.

---

## Controls

| Key | Action |
|-----|--------|
| `INSERT` | Toggle settings menu |
| `END` | Exit the tool |

When the menu is visible, the overlay captures mouse input for ImGui interaction. When hidden, all input passes through to the game.

---

## Technical Details

### Memory Access

The tool uses `ReadProcessMemory` exclusively — the `Process` class has **no write methods**. Game state is read, projected to screen coordinates, and drawn. Nothing is written back.

### Anti-Cheat Considerations

The DLL injector (`manual_mapper.cpp`, `syscalls.h`) exists solely for injecting **Dumper-7** to generate SDK dumps. It uses direct syscalls to bypass user-mode hooks from the game's anti-cheat driver. This is the only component that writes to the game's memory space, and it's a separate tool (`inject.exe`), not part of the main overlay.

### Overlay Technique

A transparent `WS_EX_LAYERED | WS_EX_TRANSPARENT` window is created on top of the game. Black pixels are made transparent via `SetLayeredWindowAttributes(LWA_COLORKEY)`. This is compatible with `DXGI_SWAP_EFFECT_DISCARD` but **not** with `FLIP_DISCARD`.

### Position Extrapolation

Entity positions are read from game memory, and velocity is computed between consecutive frames. The renderer extrapolates 1 frame ahead (configurable `EXTRAP_FACTOR`) to compensate for the read-process-draw pipeline latency, making bounding boxes feel "locked" to character models.

---

## License

[BSD 3-Clause with Non-Commercial and No-Redistribution Addendum](LICENSE)

- **Non-commercial use only** — cannot be sold or bundled with paid products
- **No redistribution of modified binaries** — prevents cheat distribution
- **No weaponization** — may not be modified to write game memory, inject input, or automate gameplay
- Integrity verification mechanisms must not be removed or circumvented

---

## Acknowledgments

- [Dear ImGui](https://github.com/ocornut/imgui) — Immediate-mode GUI library
- [spdlog](https://github.com/gabime/spdlog) — Fast C++ logging
- [nlohmann/json](https://github.com/nlohmann/json) — JSON for Modern C++
- [Dumper-7](https://github.com/Encryqed/Dumper-7) — Unreal Engine SDK generator
