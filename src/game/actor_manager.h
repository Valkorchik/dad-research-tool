#pragma once
#include "entity.h"
#include "sdk/gnames.h"
#include "sdk/gworld.h"
#include "core/config.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ActorManager {
public:
    void Update(const Process& proc, GWorldReader& world,
                GNamesReader& names, uintptr_t localPawn,
                ItemRarity minLootRarity = ItemRarity::RARE);

    const std::vector<GameEntity>& GetEntities() const { return m_entities; }

    // Get filtered entities based on settings
    std::vector<const GameEntity*> GetFiltered(
        const FVector& localPos,
        const FilterSettings& filter) const;

    // Re-read just positions for filtered entities (cheap, call every frame for smooth ESP)
    void RefreshPositions(const Process& proc, const std::vector<const GameEntity*>& filtered);

private:
    void ClassifyActor(const Process& proc, uintptr_t actorAddr,
                       GNamesReader& names, GameEntity& out,
                       uintptr_t localPawn);

    void ReadActorPosition(const Process& proc, uintptr_t actorAddr,
                           GameEntity& out);

    void ReadPlayerData(const Process& proc, uintptr_t actorAddr,
                        GNamesReader& names, GameEntity& out);

    void ReadCharacterHealth(const Process& proc, uintptr_t actorAddr,
                             GameEntity& out);

    void ReadItemData(const Process& proc, uintptr_t actorAddr,
                      GNamesReader& names, GameEntity& out);

    void ReadDeadBodyEquipment(const Process& proc, uintptr_t actorAddr,
                               GNamesReader& names, GameEntity& out,
                               ItemRarity minDisplayRarity = ItemRarity::RARE);

    void ReadMonsterData(const Process& proc, uintptr_t actorAddr,
                         GNamesReader& names, GameEntity& out);

    void ReadChestContents(const Process& proc, uintptr_t actorAddr,
                           GNamesReader& names, GameEntity& out,
                           ItemRarity minDisplayRarity = ItemRarity::RARE);

    // Process a classified entity: read type-specific data, apply portal dedup, death debounce.
    // Returns false if entity should be skipped (filtered out).
    enum class ProcessResult { KEEP, SKIP };
    ProcessResult ProcessEntity(const Process& proc, GNamesReader& names,
                                GameEntity& entity, ItemRarity minLootRarity,
                                std::unordered_set<uint64_t>& seenPortalKeys);

    static EntityType ClassifyByName(const std::string& className);
    static ItemRarity ParseRarityFromTag(const std::string& tagStr);
    static ItemRarity GuessRarityFromName(const std::string& name);

    std::vector<GameEntity> m_entities;

    // Class pointer cache: classPtr -> {type, className}
    // Avoids re-resolving FName for every actor every frame
    // ~264 unique classes out of 26k actors = 99% cache hit rate
    struct ClassInfo {
        EntityType type = EntityType::UNKNOWN;
        std::string name;
    };
    std::unordered_map<uintptr_t, ClassInfo> m_classCache;

    // Actor address cache: tracks which actor addresses have already been classified
    // as UNKNOWN (via their classPtr). Avoids re-reading classPtr for 25k actors every frame.
    // Cleared when level list changes (map transition).
    std::unordered_set<uintptr_t> m_knownUnknownActors;

    // Cached actor list — re-read from levels every N frames instead of every frame
    std::vector<uintptr_t> m_cachedActorPtrs;
    int m_actorListAge = 0;        // Frames since last full actor list refresh
    int m_lastLevelCount = 0;      // Detect map transitions
    static constexpr int ACTOR_LIST_REFRESH_FRAMES = 15;  // ~0.5s at 30fps (faster pickup of dropped items)

    // Death debounce: tracks actor addresses that were recently marked dead.
    // Value = remaining scan cycles to stay dead. Prevents skeleton fake-death flickering.
    std::unordered_map<uintptr_t, int> m_deathDebounce;
    static constexpr int DEATH_DEBOUNCE_CYCLES = 4; // ~2 seconds at 2 Hz scan rate
};
