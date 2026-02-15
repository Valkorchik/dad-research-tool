#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <string>
#include <nlohmann/json.hpp>

// Aligned with EDCItemRarity from SDK dump (DungeonCrawler_structs.hpp)
enum class ItemRarity : int {
    NONE = 0,       // EDCItemRarity::None
    POOR = 1,       // EDCItemRarity::Poor (gray)
    COMMON = 2,     // EDCItemRarity::Common (white)
    UNCOMMON = 3,   // EDCItemRarity::Uncommon (green)
    RARE = 4,       // EDCItemRarity::Rare (blue)
    EPIC = 5,       // EDCItemRarity::Epic (purple)
    LEGENDARY = 6,  // EDCItemRarity::Legend (orange)
    UNIQUE = 7,     // EDCItemRarity::Unique (gold)
    ARTIFACT = 8,   // EDCItemRarity::Artifact (red)
    COUNT
};

struct FilterSettings {
    bool showPlayers = true;
    bool showLoot = true;
    bool showNPCs = true;
    bool showPortals = true;
    float maxDistance = 300.0f;        // General max distance (NPCs, loot, chests, etc.) — slider max
    float maxPlayerDistance = 1000.0f; // Separate player distance — slider max, see everyone on the map
    ItemRarity minLootRarity = ItemRarity::UNCOMMON;
    int minMonsterGrade = 0;          // 0=All, 1=Common+, 2=Elite+, 3=Nightmare only
    bool showHealthBars = true;
    bool showDistance = true;
    bool showSnapLines = false;
};

struct VisualSettings {
    float playerColor[4] = {1.0f, 0.2f, 0.2f, 1.0f};
    float teamColor[4] = {0.2f, 1.0f, 0.2f, 1.0f};
    float npcColor[4] = {1.0f, 1.0f, 0.2f, 1.0f};
    float fontSize = 15.0f;
    int boxStyle = 0; // 0=2D Box, 1=Filled Box, 2=Corner Box
};

struct AppConfig {
    FilterSettings filter;
    VisualSettings visuals;
    int updateRateFps = 144;
    int toggleMenuKey = VK_INSERT;

    // Patterns — loaded from config so they can be updated without recompile
    std::string gworldPattern;
    std::string gnamesPattern;
    std::string gobjectsPattern;

    // Direct offsets from GSpots (used as fallback when pattern scanning fails)
    uintptr_t gworldOffset = 0xC36F138;
    uintptr_t gnamesOffset = 0xC10A3C0;
    uintptr_t gobjectsOffset = 0xC1EE260;

    // UE version string from GSpots
    std::string ueVersion = "0.15.128.8167";
};

class Config {
public:
    explicit Config(const std::string& filepath);

    void Load();
    void Save() const;

    AppConfig& Get() { return m_config; }
    const AppConfig& Get() const { return m_config; }

private:
    std::string m_filepath;
    AppConfig m_config;
};
