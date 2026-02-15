#include "core/process.h"
#include "core/pattern_scanner.h"
#include "core/config.h"
#include "core/integrity.h"
#include "sdk/ue5_types.h"
#include "sdk/gnames.h"
#include "sdk/gworld.h"
#include "game/entity.h"
#include "game/actor_manager.h"
#include "game/item_database.h"
#include "render/overlay.h"
#include "render/imgui_manager.h"
#include "render/drawing.h"
#include "render/world_to_screen.h"
#include "render/menu.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <immintrin.h>  // _mm_pause for spin-wait

// Forward declare ImGui WndProc handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void InitLogging() {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("dad_research.log", true);

    auto logger = std::make_shared<spdlog::logger>("main",
        spdlog::sinks_init_list{consoleSink, fileSink});
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(logger);

    spdlog::info("=== DAD Research Tool Starting ===");
}

static HWND FindGameWindow() {
    // Try several possible window titles
    const wchar_t* titles[] = {
        L"Dark and Darker  ",
        L"Dark and Darker",
        L"DungeonCrawler",
    };

    for (const auto* title : titles) {
        HWND hwnd = FindWindowW(nullptr, title);
        if (hwnd) return hwnd;
    }

    // Fallback: find by process and enumerate windows
    return nullptr;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // DPI awareness — render at native resolution instead of Windows bitmap upscaling.
    // Without this, the overlay looks blurry ("144p") on high-DPI or scaled displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Set Windows timer resolution to 1ms (default is 15.6ms!)
    timeBeginPeriod(1);

    // Allocate console for logging
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    InitLogging();

    // =========================================================================
    // Integrity check — verify critical source files haven't been tampered with
    // =========================================================================
    spdlog::info("Build: {}", Integrity::GetBuildFingerprint());
    auto integrity = Integrity::Verify();
    for (const auto& w : integrity.warnings) spdlog::warn("[Integrity] {}", w);
    for (const auto& f : integrity.failures) spdlog::error("[Integrity] {}", f);
    if (!integrity.passed) {
        spdlog::error("INTEGRITY CHECK FAILED — critical source files have been modified!");
        spdlog::error("This build may contain unauthorized modifications (cheats, exploits).");
        spdlog::error("Use the original source from the official repository.");
        MessageBoxW(nullptr,
            L"Integrity check failed!\n\n"
            L"Critical source files have been modified.\n"
            L"This build may contain unauthorized modifications.\n\n"
            L"Use the original source from the official repository.",
            L"DAD Research Tool - Tamper Detected", MB_OK | MB_ICONERROR);
        timeEndPeriod(1);
        FreeConsole();
        return 1;
    }

    // Load config
    Config config("config/settings.json");

    // Load item database
    ItemDatabase itemDb;
    itemDb.LoadFromFile("config/items.json");

    // =========================================================================
    // Phase 1: Attach to game process
    // =========================================================================
    Process game;
    spdlog::info("Waiting for DungeonCrawler.exe...");

    while (!game.Attach(L"DungeonCrawler.exe")) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    spdlog::info("Attached to DungeonCrawler.exe (PID: {})", game.GetPid());

    // Try multiple possible module names (anti-cheat may report different names)
    uintptr_t gameBase = game.GetModuleBase(L"DungeonCrawler.exe");
    if (!gameBase) gameBase = game.GetModuleBase(L"DungeonCrawler-Win64-Shipping.exe");
    if (!gameBase) {
        // Last resort: use whatever the first/main module is
        // The PEB fallback in Process may have stored it under the process name
        spdlog::warn("Could not find game module by name, listing available modules...");
        for (const auto& [name, info] : game.GetModules()) {
            char narrow[256] = {};
            size_t converted = 0;
            wcstombs_s(&converted, narrow, name.c_str(), sizeof(narrow) - 1);
            spdlog::info("  Module: {} at 0x{:X} (size 0x{:X})", narrow, info.base, info.size);
            // Use the largest module as game base (main exe is typically the biggest)
            if (info.size > game.GetModuleSize(L"DungeonCrawler.exe")) {
                gameBase = info.base;
            }
        }
    }
    spdlog::info("Game base address: 0x{:X}", gameBase);
    if (game.IsUsingRelay()) {
        spdlog::info("*** Using shared memory relay (version.dll proxy) for memory reads ***");
    }

    // =========================================================================
    // Phase 2: Find critical offsets via pattern scanning
    // =========================================================================
    PatternScanner scanner;

    // NOTE: These patterns are PLACEHOLDERS.
    // You MUST run GSpots or UEDumper against your game build to get real patterns.
    // The patterns below are common UE5 GWorld/GNames signatures but will likely
    // need to be updated for the specific game version.
    //
    // Use config-based patterns if available, otherwise use hardcoded defaults
    std::string gworldPattern = config.Get().gworldPattern.empty()
        ? "48 8B 05 ? ? ? ? 48 3B C3 48 0F 44 C6"  // Common UE5 GWorld pattern
        : config.Get().gworldPattern;

    std::string gnamesPattern = config.Get().gnamesPattern.empty()
        ? "48 8D 05 ? ? ? ? EB 16"  // Common UE5 GNames pattern
        : config.Get().gnamesPattern;

    // Determine which module name to use for pattern scanning
    std::wstring gameModuleName = L"DungeonCrawler.exe";
    if (!game.GetModuleBase(gameModuleName))
        gameModuleName = L"DungeonCrawler-Win64-Shipping.exe";

    spdlog::info("Scanning for GWorld...");
    auto gworldResult = scanner.FindPattern(game, gameModuleName, gworldPattern);

    spdlog::info("Scanning for GNames...");
    auto gnamesResult = scanner.FindPattern(game, gameModuleName, gnamesPattern);

    if (!gworldResult || !gnamesResult) {
        spdlog::warn("Pattern scanning failed, falling back to GSpots offsets from config...");
    }

    // Resolve addresses — try multiple sources, verify each by reading index 0 = "None"
    uintptr_t gworldAddr = 0;
    uintptr_t gnamesAddr = 0;

    // GWorld: try GSpots offset first, then pattern scan
    if (config.Get().gworldOffset) {
        gworldAddr = gameBase + config.Get().gworldOffset;
        spdlog::info("GWorld from config offset: base + 0x{:X} = 0x{:X}",
                     config.Get().gworldOffset, gworldAddr);
    }
    if (!gworldAddr && gworldResult) {
        gworldAddr = PatternScanner::ResolveRelative(game, gworldResult.value(), 3, 7);
        spdlog::info("GWorld from pattern scan: 0x{:X}", gworldAddr);
    }

    // GNames: try multiple candidate addresses and verify each one
    // Build list of candidates from all sources
    struct GNamesCandidate {
        uintptr_t addr;
        const char* source;
    };
    std::vector<GNamesCandidate> gnamesCandidates;

    // Add config offset
    if (config.Get().gnamesOffset) {
        gnamesCandidates.push_back({gameBase + config.Get().gnamesOffset,
            "config offset"});
    }
    // Add pattern scan result
    if (gnamesResult) {
        uintptr_t patternAddr = PatternScanner::ResolveRelative(game, gnamesResult.value(), 3, 7);
        if (patternAddr) gnamesCandidates.push_back({patternAddr, "pattern scan"});
    }
    // Add known Dumper-7 offsets from various game versions
    gnamesCandidates.push_back({gameBase + 0x0C10A3C0, "Dumper-7 v5.5.4"});

    // Try each candidate — verify by reading FName index 0 = "None"
    GNamesReader testNames;
    for (const auto& candidate : gnamesCandidates) {
        spdlog::info("Trying GNames candidate: 0x{:X} ({})", candidate.addr, candidate.source);
        testNames = GNamesReader();  // fresh reader
        if (testNames.Initialize(game, candidate.addr)) {
            std::string test = testNames.GetName(game, 0);
            if (test == "None") {
                gnamesAddr = candidate.addr;
                spdlog::info("GNames VERIFIED at 0x{:X} ({}) - index 0 = 'None'",
                    candidate.addr, candidate.source);
                break;
            }
            spdlog::warn("GNames candidate 0x{:X} ({}) failed - index 0 = '{}'",
                candidate.addr, candidate.source, test);
        }
    }

    if (!gnamesAddr) {
        spdlog::warn("All GNames candidates failed, using first available...");
        if (!gnamesCandidates.empty()) {
            gnamesAddr = gnamesCandidates[0].addr;
        }
    }

    if (!gworldAddr || !gnamesAddr) {
        spdlog::error("Failed to resolve GWorld/GNames addresses.");
        spdlog::error("Run GSpots and update config/settings.json offsets.");
        MessageBoxW(nullptr,
            L"Failed to find GWorld/GNames addresses.\n\n"
            L"Run GSpots to get correct offsets,\n"
            L"then update config/settings.json.",
            L"DAD Research Tool", MB_OK | MB_ICONWARNING);
        timeEndPeriod(1);
        FreeConsole();
        return 1;
    }

    spdlog::info("GWorld resolved: 0x{:X}", gworldAddr);
    spdlog::info("GNames resolved: 0x{:X}", gnamesAddr);

    // =========================================================================
    // Phase 3: Initialize UE5 readers
    // =========================================================================
    GNamesReader names;
    if (gnamesAddr) names.Initialize(game, gnamesAddr);

    GWorldReader world;
    if (gworldAddr) world.Initialize(game, gworldAddr);

    ActorManager actors;
    WorldToScreen w2s;

    // =========================================================================
    // Phase 4: Create overlay
    // =========================================================================
    HWND gameWindow = FindGameWindow();
    if (!gameWindow) {
        spdlog::error("Game window not found. Make sure the game is running and visible.");
        MessageBoxW(nullptr, L"Game window not found.", L"DAD Research Tool", MB_OK | MB_ICONERROR);
        return 1;
    }
    spdlog::info("Found game window: 0x{:X}", reinterpret_cast<uintptr_t>(gameWindow));

    Overlay overlay;
    if (!overlay.Initialize(gameWindow)) {
        spdlog::error("Failed to initialize overlay");
        return 1;
    }

    // Compute DPI scale: reference resolution is 1080p (1920x1080).
    // 4K (3840x2160) → scale 2.0, 1440p → 1.33, 1080p → 1.0.
    // Also factor in the user's font size preference (default 15 → 1.0x).
    float resScale = overlay.GetHeight() / 1080.0f;
    float fontScale = config.Get().visuals.fontSize / 15.0f;
    float dpiScale = resScale * fontScale;
    spdlog::info("DPI scaling: resolution={}x{} -> resScale={:.2f}, fontSize={:.0f} -> fontScale={:.2f}, combined={:.2f}",
        overlay.GetWidth(), overlay.GetHeight(), resScale, config.Get().visuals.fontSize, fontScale, dpiScale);

    // Apply DPI scale to all drawing functions
    Drawing::SetDpiScale(dpiScale);

    ImGuiManager imgui;
    if (!imgui.Initialize(overlay.GetOverlayHandle(), overlay.GetDevice(), overlay.GetContext(), dpiScale)) {
        spdlog::error("Failed to initialize ImGui");
        return 1;
    }

    w2s.SetScreenSize(overlay.GetWidth(), overlay.GetHeight());

    Menu menu(config.Get());
    menu.SetGWorldAddr(gworldAddr);
    menu.SetGNamesAddr(gnamesAddr);
    menu.SetAttached(true);

    // =========================================================================
    // Phase 5: Main loop
    // =========================================================================
    spdlog::info("Entering main loop");
    spdlog::info("NOTE: Is the game in Borderless Windowed mode? Overlay REQUIRES borderless/windowed, NOT exclusive fullscreen.");

    int frameCount = 0;
    auto fpsTimer = std::chrono::steady_clock::now();

    // Thread-safe entity sharing between scan thread and render thread
    std::mutex entityMutex;
    std::vector<GameEntity> sharedEntities;       // Written by scan thread
    std::vector<GameEntity> renderEntities;        // Copy used by render thread
    std::vector<const GameEntity*> filtered;       // Filtered pointers into renderEntities
    std::atomic<bool> running{true};
    std::atomic<bool> newDataAvailable{false};
    std::atomic<int> scanEntityCount{0};

    // =========================================================================
    // Background actor scan thread — does the heavy work without blocking render
    // =========================================================================
    std::thread scanThread([&]() {
        spdlog::info("[ScanThread] Started");
        while (running.load()) {
            if (!game.IsValid()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            auto scanStart = std::chrono::steady_clock::now();

            // Heavy actor scan
            uintptr_t localPawn = world.GetLocalPawn(game);
            actors.Update(game, world, names, localPawn, config.Get().filter.minLootRarity);

            // Copy entities under lock
            {
                std::lock_guard<std::mutex> lock(entityMutex);
                sharedEntities = actors.GetEntities(); // deep copy
                scanEntityCount.store(static_cast<int>(sharedEntities.size()));
                newDataAvailable.store(true);
            }

            auto scanEnd = std::chrono::steady_clock::now();
            float scanMs = std::chrono::duration<float, std::milli>(scanEnd - scanStart).count();

            static int diagCount = 0;
            if (diagCount++ % 60 == 0) {  // Every ~30s instead of 5s
                spdlog::info("[ScanThread] Scan took {:.0f}ms, {} entities", scanMs, actors.GetEntities().size());
            }

            // Wait before next scan (500ms minus scan time, minimum 50ms)
            float waitMs = (std::max)(50.0f, 500.0f - scanMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(waitMs)));
        }
        spdlog::info("[ScanThread] Stopped");
    });

    // Frame rate limiter: cap overlay to target FPS to avoid starving the game GPU.
    // A transparent overlay doesn't need 1000+ fps — 144 is buttery smooth.
    const float targetFrameTime = 1.0f / static_cast<float>(config.Get().updateRateFps);
    auto lastFrameTime = std::chrono::steady_clock::now();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Frame rate limiter — sleep until next frame is due
        {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastFrameTime).count();
            if (elapsed < targetFrameTime) {
                // Sleep for most of the remaining time (leave 1ms margin for accuracy)
                float sleepMs = (targetFrameTime - elapsed) * 1000.0f - 1.0f;
                if (sleepMs > 1.0f) {
                    std::this_thread::sleep_for(std::chrono::microseconds(
                        static_cast<int64_t>(sleepMs * 1000.0f)));
                }
                // Spin-wait for the last fraction of a ms (precise timing)
                while (std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - lastFrameTime).count() < targetFrameTime) {
                    _mm_pause(); // CPU hint: we're spin-waiting
                }
            }
            lastFrameTime = std::chrono::steady_clock::now();
        }

        // Check if game is still running
        if (!game.IsValid()) {
            spdlog::warn("Game process terminated");
            break;
        }

        // Toggle menu with INSERT key
        if (GetAsyncKeyState(config.Get().toggleMenuKey) & 1) {
            menu.Toggle();
            overlay.SetClickThrough(!menu.IsVisible());
        }

        // Exit with END key
        if (GetAsyncKeyState(VK_END) & 1) {
            spdlog::info("Exit key pressed");
            break;
        }

        // FPS counter
        frameCount++;
        auto fpsNow = std::chrono::steady_clock::now();
        float fpsElapsed = std::chrono::duration<float>(fpsNow - fpsTimer).count();
        if (fpsElapsed >= 1.0f) {
            menu.SetFps(frameCount / fpsElapsed);
            frameCount = 0;
            fpsTimer = fpsNow;
        }

        // Update screen size
        w2s.SetScreenSize(overlay.GetWidth(), overlay.GetHeight());

        // =====================================================================
        // Update game data — render thread only does cheap work
        // =====================================================================
        if (gworldAddr && gnamesAddr) {
            // Pick up new entity data from scan thread (non-blocking)
            if (newDataAvailable.load()) {
                std::lock_guard<std::mutex> lock(entityMutex);
                renderEntities = std::move(sharedEntities);
                newDataAvailable.store(false);

                // Rebuild filtered list from new render entities
                FVector localPos = w2s.GetCameraPosition();
                filtered.clear();
                for (const auto& entity : renderEntities) {
                    if (entity.isLocalPlayer) continue;
                    switch (entity.type) {
                        case EntityType::PLAYER: if (!config.Get().filter.showPlayers) continue; break;
                        case EntityType::MONSTER:
                            if (!config.Get().filter.showNPCs) continue;
                            if (config.Get().filter.minMonsterGrade > 0 &&
                                static_cast<int>(entity.monsterGrade) < config.Get().filter.minMonsterGrade) continue;
                            break;
                        case EntityType::CHEST_NORMAL: case EntityType::CHEST_SPECIAL: break;
                        case EntityType::LOOT_ITEM: if (!config.Get().filter.showLoot) continue; break;
                        case EntityType::PORTAL: if (!config.Get().filter.showPortals) continue; break;
                        case EntityType::INTERACTABLE: break;
                        default: continue;
                    }
                    float dist = static_cast<float>(entity.position.DistanceToMeters(localPos));
                    // Players get their own (much larger) distance filter
                    float maxDist = (entity.type == EntityType::PLAYER)
                        ? config.Get().filter.maxPlayerDistance
                        : config.Get().filter.maxDistance;
                    if (dist > maxDist) continue;
                    if (entity.type == EntityType::LOOT_ITEM &&
                        entity.rarity < config.Get().filter.minLootRarity) continue;
                    filtered.push_back(&entity);
                }
                menu.SetEntityCount(static_cast<int>(filtered.size()));
            }

            // =================================================================
            // STEP 1: Read all world positions + bones (no projection yet)
            // We read all positions FIRST, then camera LAST, then project.
            // This minimizes time between camera read and drawing,
            // which eliminates the "blinking when turning" artifact.
            //
            // Position extrapolation: we compute velocity from consecutive
            // reads and extrapolate forward to compensate for read latency.
            // =================================================================
            constexpr uintptr_t C2W_OFFSET = 0x30;
            for (const auto* entityPtr : filtered) {
                auto& entity = const_cast<GameEntity&>(*entityPtr);

                // Cache rootComp pointer (doesn't change between frames)
                if (!entity.cachedRootComp) {
                    entity.cachedRootComp = game.Read<uintptr_t>(entity.address + UEOffsets::AActor_RootComponent);
                }
                if (!entity.cachedRootComp) continue;

                // Read world position from ComponentToWorld
                FVector rawPos = game.Read<FVector>(entity.cachedRootComp + UEOffsets::SceneComp_ComponentToWorld + C2W_OFFSET);
                if (rawPos.X == 0.0 && rawPos.Y == 0.0 && rawPos.Z == 0.0)
                    rawPos = game.Read<FVector>(entity.cachedRootComp + UEOffsets::SceneComp_RelativeLocation);
                if (std::abs(rawPos.Z) >= 100000.0)
                    continue; // Skip invalid position

                entity.position = rawPos;
                entity.hasBoneData = false;

                // For characters: read health + bone positions from SkeletalMeshComponent
                if (entity.type == EntityType::PLAYER || entity.type == EntityType::MONSTER) {
                    // Fast health read: use cached AttrSet pointer (1 read, no chain traversal)
                    if (entity.cachedAttrSet) {
                        float newHealth = game.Read<float>(
                            entity.cachedAttrSet + UEOffsets::Attr_Health + UEOffsets::AttrData_CurrentValue);
                        if (newHealth >= 0.0f && newHealth < 10000.0f) {
                            entity.health = newHealth;
                            // Death is STICKY: once dead, only revive if health is substantial
                            // (> 20% max). This prevents flickering from stale/garbage reads.
                            // The scan thread is the authority for alive→dead transitions.
                            if (entity.isAlive) {
                                // Currently alive: can transition to dead
                                entity.isAlive = newHealth >= 0.5f;
                            } else {
                                // Currently dead: only revive if clearly alive (real revive/heal)
                                float reviveThreshold = entity.maxHealth > 0 ? entity.maxHealth * 0.2f : 10.0f;
                                entity.isAlive = newHealth >= reviveThreshold;
                            }
                        }
                    }
                    // Cache mesh component pointer (doesn't change between frames)
                    if (!entity.cachedMeshComp) {
                        entity.cachedMeshComp = game.Read<uintptr_t>(entity.address + UEOffsets::ACharacter_Mesh);
                    }

                    // Raw bone positions (before extrapolation)
                    FVector rawBoneHead{}, rawBoneFeet{};

                    if (entity.cachedMeshComp) {
                        // Read mesh component world position
                        FVector meshWorldPos = game.Read<FVector>(
                            entity.cachedMeshComp + UEOffsets::SceneComp_ComponentToWorld + C2W_OFFSET);

                        // Read bone array header
                        TArray<FTransform> boneArray = game.Read<TArray<FTransform>>(
                            entity.cachedMeshComp + UEOffsets::SkelMesh_CachedComponentSpaceTransforms);

                        if (boneArray.Data && boneArray.Count > 2 && boneArray.Count < 300) {
                            // Root bone (index 0) = feet
                            FVector rootBoneLocal = game.Read<FVector>(
                                boneArray.Data + UEOffsets::FTransform_Translation);

                            FVector bestHeadLocal = rootBoneLocal;

                            if (entity.cachedHeadBoneIdx >= 0 && entity.cachedHeadBoneIdx < boneArray.Count) {
                                // Fast path: reuse cached bone index (1 read instead of 7)
                                bestHeadLocal = game.Read<FVector>(
                                    boneArray.Data + entity.cachedHeadBoneIdx * UEOffsets::FTransform_Size
                                    + UEOffsets::FTransform_Translation);
                            } else {
                                // First time: scan candidate bones and cache the best one.
                                // UE5 humanoid mannequin uses index 67, but non-humanoid monsters
                                // (gargoyles, etc.) have different skeletons.
                                double bestHeadZ = rootBoneLocal.Z;
                                int bestIdx = 0;

                                int candidates[] = {
                                    (std::min)(67, boneArray.Count - 1),
                                    (std::min)(6, boneArray.Count - 1),
                                    (std::min)(10, boneArray.Count - 1),
                                    (std::min)(20, boneArray.Count - 1),
                                    boneArray.Count / 2,
                                    boneArray.Count * 3 / 4,
                                    boneArray.Count - 1,
                                };

                                for (int idx : candidates) {
                                    if (idx <= 0 || idx >= boneArray.Count) continue;
                                    FVector boneLocal = game.Read<FVector>(
                                        boneArray.Data + idx * UEOffsets::FTransform_Size
                                        + UEOffsets::FTransform_Translation);
                                    if (boneLocal.Z > bestHeadZ) {
                                        bestHeadZ = boneLocal.Z;
                                        bestHeadLocal = boneLocal;
                                        bestIdx = idx;
                                    }
                                }
                                entity.cachedHeadBoneIdx = bestIdx;
                            }

                            // Component-space → world-space
                            rawBoneFeet = {meshWorldPos.X + rootBoneLocal.X,
                                           meshWorldPos.Y + rootBoneLocal.Y,
                                           meshWorldPos.Z + rootBoneLocal.Z};
                            rawBoneHead = {meshWorldPos.X + bestHeadLocal.X,
                                           meshWorldPos.Y + bestHeadLocal.Y,
                                           meshWorldPos.Z + bestHeadLocal.Z};

                            // Validate: head must be above feet, reasonable height, valid position
                            double boneHeight = rawBoneHead.Z - rawBoneFeet.Z;
                            if (boneHeight > 10.0 &&  // At least 10 UU (~10cm) tall
                                boneHeight < 500.0 &&
                                std::abs(rawBoneFeet.Z) < 100000.0) {
                                entity.hasBoneData = true;
                                entity.position = rawBoneFeet;
                                entity.headPosition = rawBoneHead;
                                entity.boneHead = rawBoneHead;
                                entity.boneFeet = rawBoneFeet;
                            }
                        }
                    }

                    if (!entity.hasBoneData) {
                        entity.headPosition = entity.position;
                        entity.headPosition.Z += 170.0;
                    }

                    // ---------------------------------------------------------
                    // Position extrapolation: predict where the entity IS NOW
                    // based on where it WAS (prevPosition = last frame's raw read)
                    // and where it IS (this frame's raw read).
                    //
                    // The key insight: by the time we finish reading positions,
                    // building the projection matrix, and drawing, the entity
                    // has moved ~1 more frame ahead. So we extrapolate by 1.0x
                    // the inter-frame velocity to cancel out the render pipeline
                    // latency and make boxes feel "locked" to the model.
                    //
                    // prevPosition stores the RAW read (not extrapolated), so
                    // velocity = raw_this_frame - raw_last_frame = true movement.
                    // ---------------------------------------------------------
                    if (entity.hasPrevPosition) {
                        // Use raw position for velocity (not extrapolated)
                        FVector rawThisFrame = entity.hasBoneData ? rawBoneFeet : rawPos;
                        double vx = rawThisFrame.X - entity.prevPosition.X;
                        double vy = rawThisFrame.Y - entity.prevPosition.Y;
                        double vz = rawThisFrame.Z - entity.prevPosition.Z;

                        // Only extrapolate if movement is reasonable
                        // (> 0.1 UU to avoid jitter on stationary, < 500 UU to avoid teleport)
                        double speedSq = vx * vx + vy * vy + vz * vz;
                        if (speedSq > 0.01 && speedSq < 250000.0) {
                            // 1.0x = predict 1 full frame ahead (cancels render pipeline lag)
                            constexpr double EXTRAP_FACTOR = 1.0;
                            entity.position.X += vx * EXTRAP_FACTOR;
                            entity.position.Y += vy * EXTRAP_FACTOR;
                            entity.position.Z += vz * EXTRAP_FACTOR;

                            if (entity.hasBoneData) {
                                // Bone velocity (head/feet may differ from root during animation)
                                double hvx = rawBoneHead.X - entity.prevBoneHead.X;
                                double hvy = rawBoneHead.Y - entity.prevBoneHead.Y;
                                double hvz = rawBoneHead.Z - entity.prevBoneHead.Z;
                                entity.boneHead.X += hvx * EXTRAP_FACTOR;
                                entity.boneHead.Y += hvy * EXTRAP_FACTOR;
                                entity.boneHead.Z += hvz * EXTRAP_FACTOR;
                                entity.boneFeet.X += vx * EXTRAP_FACTOR;
                                entity.boneFeet.Y += vy * EXTRAP_FACTOR;
                                entity.boneFeet.Z += vz * EXTRAP_FACTOR;
                                entity.headPosition = entity.boneHead;
                            } else {
                                entity.headPosition.X += vx * EXTRAP_FACTOR;
                                entity.headPosition.Y += vy * EXTRAP_FACTOR;
                                entity.headPosition.Z += vz * EXTRAP_FACTOR;
                            }
                        }
                    }

                    // Store RAW read (not extrapolated) for next frame's velocity calc
                    entity.prevPosition = entity.hasBoneData ? rawBoneFeet : rawPos;
                    entity.prevBoneHead = rawBoneHead;
                    entity.prevBoneFeet = rawBoneFeet;
                    entity.hasPrevPosition = true;
                }
            }

            // =================================================================
            // STEP 2: Read camera AS LATE AS POSSIBLE — right before projection
            // Uses cached CameraManager pointer (1 read instead of 7)
            // =================================================================
            w2s.UpdateCameraFast(game, world);
            FVector localPos = w2s.GetCameraPosition();

            // =================================================================
            // STEP 3: Project all positions to screen (pure CPU math, instant)
            // Camera and positions are now maximally synchronized
            // =================================================================
            for (const auto* entity : filtered) {
                auto& mutableEntity = const_cast<GameEntity&>(*entity);
                mutableEntity.isOnScreen = w2s.Project(entity->position, mutableEntity.screenPos);
                mutableEntity.distanceMeters = static_cast<float>(entity->position.DistanceToMeters(localPos));

                if (entity->hasBoneData) {
                    mutableEntity.headOnScreen = w2s.Project(entity->boneHead, mutableEntity.screenHead);
                    mutableEntity.feetOnScreen = w2s.Project(entity->boneFeet, mutableEntity.screenFeet);
                }
            }

            // =================================================================
            // Render overlay
            // =================================================================
            overlay.BeginFrame();
            imgui.BeginFrame();

            ImDrawList* draw = ImGui::GetBackgroundDrawList();

            // Show loading indicator until first scan data arrives
            if (filtered.empty() && scanEntityCount.load() == 0) {
                Drawing::DrawLoadingStatus(draw, overlay.GetWidth(), overlay.GetHeight(),
                    "Scanning for actors...", -1.0f);
            }

            for (const auto* entity : filtered) {
                if (!entity->isOnScreen) continue;

                switch (entity->type) {
                    case EntityType::PLAYER: {
                        ImU32 color = IM_COL32(
                            static_cast<int>(config.Get().visuals.playerColor[0] * 255),
                            static_cast<int>(config.Get().visuals.playerColor[1] * 255),
                            static_cast<int>(config.Get().visuals.playerColor[2] * 255),
                            static_cast<int>(config.Get().visuals.playerColor[3] * 255)
                        );
                        Drawing::DrawPlayerESP(draw, *entity, color, config.Get().visuals.boxStyle);

                        if (config.Get().filter.showSnapLines) {
                            Drawing::DrawSnapLine(draw,
                                ImVec2(entity->screenPos.X, entity->screenPos.Y),
                                overlay.GetWidth(), overlay.GetHeight(), color);
                        }
                        break;
                    }

                    case EntityType::LOOT_ITEM:
                        Drawing::DrawItemESP(draw, *entity);
                        break;

                    case EntityType::MONSTER: {
                        ImU32 color = IM_COL32(
                            static_cast<int>(config.Get().visuals.npcColor[0] * 255),
                            static_cast<int>(config.Get().visuals.npcColor[1] * 255),
                            static_cast<int>(config.Get().visuals.npcColor[2] * 255),
                            static_cast<int>(config.Get().visuals.npcColor[3] * 255)
                        );
                        Drawing::DrawPlayerESP(draw, *entity, color, config.Get().visuals.boxStyle);
                        break;
                    }

                    case EntityType::CHEST_SPECIAL: {
                        // Rare chests get bright gold highlight
                        ImU32 gold = IM_COL32(255, 215, 0, 255);
                        Drawing::DrawPlayerESP(draw, *entity, gold, 0);
                        break;
                    }

                    case EntityType::CHEST_NORMAL: {
                        // Normal chests get subtle white
                        ImU32 white = IM_COL32(200, 200, 200, 180);
                        Drawing::DrawPlayerESP(draw, *entity, white, 0);
                        break;
                    }

                    case EntityType::PORTAL: {
                        // Portals in blue
                        ImU32 blue = IM_COL32(50, 150, 255, 255);
                        Drawing::DrawPlayerESP(draw, *entity, blue, 0);
                        break;
                    }

                    default:
                        break;
                }
            }

            // Radar minimap
            Drawing::DrawRadar(draw, w2s.GetCameraPosition(),
                static_cast<float>(w2s.GetCameraRotation().Yaw),
                filtered, overlay.GetWidth(), overlay.GetHeight());

            // Render menu
            menu.Render();

            imgui.EndFrame();
            overlay.EndFrame();
        } else {
            // No game data — just render empty overlay with menu
            overlay.BeginFrame();
            imgui.BeginFrame();
            menu.Render();
            imgui.EndFrame();
            overlay.EndFrame();
        }
    }

    // =========================================================================
    // Cleanup
    // =========================================================================
    spdlog::info("Shutting down...");
    running.store(false);
    if (scanThread.joinable()) scanThread.join();
    config.Save();
    imgui.Shutdown();
    overlay.Shutdown();
    game.Detach();

    timeEndPeriod(1);
    FreeConsole();
    return 0;
}
