#include "actor_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <functional>

// ============================================================================
// ProcessEntity: shared logic for both cache-hit and cache-miss paths.
// Reads type-specific data, applies portal dedup, death debounce.
// Returns SKIP if entity should be filtered out.
// ============================================================================
ActorManager::ProcessResult ActorManager::ProcessEntity(
    const Process& proc, GNamesReader& names,
    GameEntity& entity, ItemRarity minLootRarity,
    std::unordered_set<uint64_t>& seenPortalKeys)
{
    switch (entity.type) {
        case EntityType::PLAYER:
            ReadPlayerData(proc, entity.address, names, entity);
            entity.headPosition = entity.position;
            entity.headPosition.Z += 170.0;
            if (!entity.isAlive && !entity.isLocalPlayer) {
                ReadDeadBodyEquipment(proc, entity.address, names, entity, minLootRarity);
            }
            break;
        case EntityType::MONSTER:
            ReadMonsterData(proc, entity.address, names, entity);
            if (!entity.isAlive) {
                ReadDeadBodyEquipment(proc, entity.address, names, entity, minLootRarity);
            }
            break;
        case EntityType::CHEST_SPECIAL:
            entity.displayName = "RARE CHEST";
            ReadChestContents(proc, entity.address, names, entity, minLootRarity);
            break;
        case EntityType::CHEST_NORMAL:
            entity.displayName = "Chest";
            ReadChestContents(proc, entity.address, names, entity, minLootRarity);
            break;
        case EntityType::LOOT_ITEM:
            ReadItemData(proc, entity.address, names, entity);
            if (entity.type == EntityType::UNKNOWN) return ProcessResult::SKIP;
            break;
        case EntityType::PORTAL: {
            // Portal deduplication: pack position into 64-bit key (50 UU grid)
            int px = static_cast<int>(entity.position.X / 50.0);
            int py = static_cast<int>(entity.position.Y / 50.0);
            int pz = static_cast<int>(entity.position.Z / 50.0);
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(px)) << 32)
                         | (static_cast<uint64_t>(static_cast<uint16_t>(py)) << 16)
                         | static_cast<uint64_t>(static_cast<uint16_t>(pz));
            if (seenPortalKeys.count(key)) return ProcessResult::SKIP;
            seenPortalKeys.insert(key);

            // Descriptive portal names
            if (entity.className.find("Escape") != std::string::npos ||
                entity.className.find("Extraction") != std::string::npos ||
                entity.className.find("StairEscape") != std::string::npos)
                entity.displayName = "EXIT PORTAL";
            else if (entity.className.find("Down") != std::string::npos)
                entity.displayName = "Down Portal";
            else if (entity.className.find("Up") != std::string::npos)
                entity.displayName = "Up Portal";
            else
                entity.displayName = "Portal";
            break;
        }
        default:
            entity.displayName = entity.className;
            break;
    }

    // Death debounce: prevent skeleton fake-death flickering
    if (entity.type == EntityType::MONSTER) {
        auto debounceIt = m_deathDebounce.find(entity.address);
        if (!entity.isAlive && entity.health >= 0.0f && entity.health <= 0.0f) {
            m_deathDebounce[entity.address] = DEATH_DEBOUNCE_CYCLES;
        } else if (debounceIt != m_deathDebounce.end() && entity.health >= 0.0f && entity.health <= 0.0f) {
            entity.isAlive = false;
        } else if (debounceIt != m_deathDebounce.end() && entity.health > 0.0f) {
            m_deathDebounce.erase(debounceIt);
        }
    }

    return ProcessResult::KEEP;
}

void ActorManager::Update(const Process& proc, GWorldReader& world,
                          GNamesReader& names, uintptr_t localPawn,
                          ItemRarity minLootRarity) {
    m_entities.clear();
    static int diagCounter = 0;
    bool doDiag = (diagCounter++ % 900 == 0); // Periodic diagnostics every ~30s (production)

    // Tick down death debounce counters
    for (auto it = m_deathDebounce.begin(); it != m_deathDebounce.end(); ) {
        it->second--;
        if (it->second <= 0)
            it = m_deathDebounce.erase(it);
        else
            ++it;
    }

    // =========================================================================
    // OPTIMIZATION 1: Cache the actor pointer list, refresh every N frames
    // =========================================================================
    m_actorListAge++;
    bool needRefresh = m_actorListAge >= ACTOR_LIST_REFRESH_FRAMES || m_cachedActorPtrs.empty();

    if (needRefresh) {
        auto newActorPtrs = world.GetAllActors(proc);

        int levelCount = static_cast<int>(newActorPtrs.size());
        if (m_lastLevelCount > 0 && std::abs(levelCount - m_lastLevelCount) > m_lastLevelCount / 2) {
            m_knownUnknownActors.clear();
            m_deathDebounce.clear();
            spdlog::info("[ActorManager] Map transition detected ({} -> {} actors), clearing caches",
                m_lastLevelCount, levelCount);
        }
        m_lastLevelCount = levelCount;
        m_cachedActorPtrs = std::move(newActorPtrs);
        m_actorListAge = 0;
    }

    const auto& actorPtrs = m_cachedActorPtrs;

    if (doDiag) {
        spdlog::info("[ActorManager] {} raw actors, localPawn=0x{:X}, classCache={}, addrCache={}",
            actorPtrs.size(), localPawn, m_classCache.size(), m_knownUnknownActors.size());
    }

    m_entities.reserve(256);
    std::unordered_set<uint64_t> seenPortalKeys;

    int skippedByAddrCache = 0, cacheHits = 0, cacheMisses = 0, classified = 0;
    constexpr int MAX_CACHE_MISSES_PER_FRAME = 500;

    for (uintptr_t actorAddr : actorPtrs) {
        // OPTIMIZATION 2: Skip known-unknown actors by address
        if (m_knownUnknownActors.count(actorAddr)) {
            skippedByAddrCache++;
            continue;
        }

        // Step 1: Read classPtr
        uintptr_t classPtr = proc.Read<uintptr_t>(actorAddr + UEOffsets::UObject_Class);
        if (!classPtr || classPtr > 0x00007FFFFFFFFFFF) {
            m_knownUnknownActors.insert(actorAddr);
            continue;
        }

        // Step 2: Resolve class name + type (from cache or fresh)
        EntityType type = EntityType::UNKNOWN;
        std::string cname;

        auto cacheIt = m_classCache.find(classPtr);
        if (cacheIt != m_classCache.end()) {
            cacheHits++;
            type = cacheIt->second.type;
            cname = cacheIt->second.name;
        } else {
            // Cache miss — resolve FName
            if (cacheMisses >= MAX_CACHE_MISSES_PER_FRAME) continue;
            cacheMisses++;

            FName className = proc.Read<FName>(classPtr + UEOffsets::UObject_Name);
            cname = names.GetFName(proc, className.ComparisonIndex, className.Number);
            if (!cname.empty()) type = ClassifyByName(cname);
            m_classCache[classPtr] = {type, cname};

            if (type != EntityType::UNKNOWN) {
                spdlog::info("[ActorManager] NEW class: '{}' -> {} (0x{:X})",
                    cname, static_cast<int>(type), classPtr);
            }
        }

        // Skip UNKNOWN types
        if (type == EntityType::UNKNOWN) {
            m_knownUnknownActors.insert(actorAddr);
            // Diagnostic for druid shapeshift discovery
            if (!cname.empty() && (cname.find("Bear") != std::string::npos ||
                cname.find("Druid") != std::string::npos ||
                cname.find("Panther") != std::string::npos ||
                cname.find("Shift") != std::string::npos)) {
                spdlog::info("[ClassifyDiag] UNKNOWN with animal/druid keyword: '{}'", cname);
            }
            continue;
        }

        // Step 3: Build entity and read position
        GameEntity entity;
        entity.address = actorAddr;
        entity.className = cname;
        entity.type = type;
        entity.isLocalPlayer = (actorAddr == localPawn);

        ReadActorPosition(proc, actorAddr, entity);
        if (entity.position.X == 0.0 && entity.position.Y == 0.0 && entity.position.Z == 0.0)
            continue;
        if (std::abs(entity.position.Z) > 100000.0)
            continue;

        // Step 4: Read type-specific data via shared helper
        if (ProcessEntity(proc, names, entity, minLootRarity, seenPortalKeys) == ProcessResult::SKIP)
            continue;

        m_entities.push_back(std::move(entity));
        classified++;
    }

    if (doDiag) {
        spdlog::info("[ActorManager] {} actors -> {} entities | skip={}, hit={}, miss={}",
            actorPtrs.size(), m_entities.size(), skippedByAddrCache, cacheHits, cacheMisses);

        int players = 0, monsters = 0, chests = 0, loot = 0;
        for (const auto& e : m_entities) {
            switch (e.type) {
                case EntityType::PLAYER: players++; break;
                case EntityType::MONSTER: monsters++; break;
                case EntityType::CHEST_SPECIAL:
                case EntityType::CHEST_NORMAL: chests++; break;
                case EntityType::LOOT_ITEM: loot++; break;
                default: break;
            }
        }
        spdlog::info("[ActorManager] Breakdown: {} players, {} monsters, {} chests, {} loot",
            players, monsters, chests, loot);
    }
}

std::vector<const GameEntity*> ActorManager::GetFiltered(
    const FVector& localPos,
    const FilterSettings& filter) const
{
    std::vector<const GameEntity*> result;

    for (const auto& entity : m_entities) {
        if (entity.isLocalPlayer)
            continue;

        // Type filter
        switch (entity.type) {
            case EntityType::PLAYER:
                if (!filter.showPlayers) continue;
                break;
            case EntityType::MONSTER:
                if (!filter.showNPCs) continue;
                // Monster grade filter: 0=All, 1=Common+, 2=Elite+, 3=Nightmare
                if (filter.minMonsterGrade > 0 &&
                    static_cast<int>(entity.monsterGrade) < filter.minMonsterGrade)
                    continue;
                break;
            case EntityType::CHEST_NORMAL:
            case EntityType::CHEST_SPECIAL:
                break;
            case EntityType::LOOT_ITEM:
                if (!filter.showLoot) continue;
                break;
            case EntityType::PORTAL:
                if (!filter.showPortals) continue;
                break;
            case EntityType::INTERACTABLE:
                break;
            default:
                continue;
        }

        // Distance filter — players get their own (larger) distance
        float dist = static_cast<float>(entity.position.DistanceToMeters(localPos));
        float maxDist = (entity.type == EntityType::PLAYER)
            ? filter.maxPlayerDistance
            : filter.maxDistance;
        if (dist > maxDist)
            continue;

        // Rarity filter for loot
        if (entity.type == EntityType::LOOT_ITEM &&
            entity.rarity < filter.minLootRarity)
            continue;

        result.push_back(&entity);
    }

    return result;
}

void ActorManager::RefreshPositions(const Process& proc,
                                     const std::vector<const GameEntity*>& filtered) {
    constexpr uintptr_t C2W_TRANSLATION_OFFSET = 0x30;

    for (const auto* entityPtr : filtered) {
        auto& entity = const_cast<GameEntity&>(*entityPtr);

        uintptr_t rootComp = proc.Read<uintptr_t>(entity.address + UEOffsets::AActor_RootComponent);
        if (!rootComp) continue;

        FVector newPos = proc.Read<FVector>(
            rootComp + UEOffsets::SceneComp_ComponentToWorld + C2W_TRANSLATION_OFFSET);

        // Fallback if C2W returns zeros
        if (newPos.X == 0.0 && newPos.Y == 0.0 && newPos.Z == 0.0) {
            newPos = proc.Read<FVector>(rootComp + UEOffsets::SceneComp_RelativeLocation);
        }

        // Only update if we got a valid position (skip inventory items)
        if (std::abs(newPos.Z) < 100000.0) {
            entity.position = newPos;
            if (entity.type == EntityType::PLAYER) {
                entity.headPosition = newPos;
                entity.headPosition.Z += 170.0;
            }
        }
    }
}

void ActorManager::ClassifyActor(const Process& proc, uintptr_t actorAddr,
                                  GNamesReader& names, GameEntity& out,
                                  uintptr_t localPawn) {
    // Read UObject::Class pointer
    uintptr_t classPtr = proc.Read<uintptr_t>(actorAddr + UEOffsets::UObject_Class);
    if (!classPtr) return;

    // Read class FName
    FName className = proc.Read<FName>(classPtr + UEOffsets::UObject_Name);
    out.className = names.GetFName(proc, className.ComparisonIndex, className.Number);

    if (out.className.empty()) return;

    // Classify by class name patterns (using real DaD class names from SDK)
    out.type = ClassifyByName(out.className);

    if (out.type == EntityType::UNKNOWN) return;

    // Read position for all classified actors
    ReadActorPosition(proc, actorAddr, out);

    // Check if this is the local player
    out.isLocalPlayer = (actorAddr == localPawn);

    // Read type-specific data
    switch (out.type) {
        case EntityType::PLAYER:
            ReadPlayerData(proc, actorAddr, names, out);
            out.headPosition = out.position;
            out.headPosition.Z += 170.0; // ~170cm standing height
            break;

        case EntityType::MONSTER:
            ReadCharacterHealth(proc, actorAddr, out);
            out.displayName = out.className;
            break;

        case EntityType::CHEST_SPECIAL:
            out.displayName = "RARE CHEST";
            break;

        case EntityType::CHEST_NORMAL:
            out.displayName = "Chest";
            break;

        case EntityType::LOOT_ITEM:
            ReadItemData(proc, actorAddr, names, out);
            break;

        case EntityType::PORTAL:
            out.displayName = "Portal";
            break;

        default:
            out.displayName = out.className;
            break;
    }
}

void ActorManager::ReadActorPosition(const Process& proc, uintptr_t actorAddr,
                                      GameEntity& out) {
    uintptr_t rootComp = proc.Read<uintptr_t>(actorAddr + UEOffsets::AActor_RootComponent);
    if (!rootComp) return;

    // Read world position from ComponentToWorld FTransform.
    // This is the RESOLVED world-space transform — handles parented components and level offsets.
    // RelativeLocation only gives position relative to parent — Z=0 for many ground items.
    //
    // UE5 FTransform memory layout (SIMD-aligned VectorRegister4Double):
    //   +0x00: FQuat Rotation (padded to 48 bytes with extra alignment data)
    //   +0x30: FVector Translation (X, Y, Z as 3 doubles = 24 bytes)
    //   +0x48: padding (8 bytes)
    //   +0x50: FVector Scale3D (X, Y, Z as 3 doubles = 24 bytes)
    // Verified via raw hex dump: translation at +0x30 matches RelativeLocation for root actors.
    constexpr uintptr_t C2W_TRANSLATION_OFFSET = 0x30; // offset within FTransform to translation XYZ
    out.position = proc.Read<FVector>(
        rootComp + UEOffsets::SceneComp_ComponentToWorld + C2W_TRANSLATION_OFFSET);

    // Fallback: if ComponentToWorld gave all zeros, try RelativeLocation
    if (out.position.X == 0.0 && out.position.Y == 0.0 && out.position.Z == 0.0) {
        out.position = proc.Read<FVector>(rootComp + UEOffsets::SceneComp_RelativeLocation);
    }

    out.rotation = proc.Read<FRotator>(rootComp + UEOffsets::SceneComp_RelativeRotation);
}

void ActorManager::ReadPlayerData(const Process& proc, uintptr_t actorAddr,
                                   GNamesReader& names, GameEntity& out) {
    // Read player nickname from AccountDataReplication chain
    // ADCCharacterBase + 0x07F8 = FAccountDataReplication
    // FAccountDataReplication + 0x0010 = FNickname
    // FNickname + 0x0000 = FString (OriginalNickName)
    uintptr_t acctDataAddr = actorAddr + UEOffsets::DCChar_AccountDataReplication;
    uintptr_t nicknameStrAddr = acctDataAddr + UEOffsets::AcctData_Nickname + UEOffsets::Nickname_OriginalNickName;

    // Read FString: { wchar_t* Data, int32 Count, int32 Max }
    FString nameStr = proc.Read<FString>(nicknameStrAddr);
    if (nameStr.Data && nameStr.Count > 0 && nameStr.Count < 128) {
        std::vector<wchar_t> wbuf(nameStr.Count);
        if (proc.ReadRaw(nameStr.Data, wbuf.data(), nameStr.Count * sizeof(wchar_t))) {
            // Convert wchar to utf8
            std::string utf8Name;
            for (int i = 0; i < nameStr.Count && wbuf[i] != 0; i++) {
                if (wbuf[i] < 128)
                    utf8Name += static_cast<char>(wbuf[i]);
                else
                    utf8Name += '?'; // Non-ASCII placeholder
            }
            if (!utf8Name.empty())
                out.displayName = utf8Name;
        }
    }

    // Read player level from AccountDataReplication
    out.gearScore = proc.Read<int32_t>(acctDataAddr + UEOffsets::AcctData_Level);

    // Check alive status from AccountDataReplication
    bool bAlive = proc.Read<bool>(acctDataAddr + UEOffsets::AcctData_bAlive);
    bool bDown = proc.Read<bool>(acctDataAddr + UEOffsets::AcctData_bDown);

    // ---- Determine player class ----
    // CharacterId FString at AcctData+0x0080 (confirmed via runtime scan, Feb 2026)
    // Contains: "DesignDataPlayerCharacter:Id_PlayerCharacter_Fighter" etc.
    auto parseClass = [](const std::string& s) -> std::string {
        if (s.find("Fighter") != std::string::npos)    return "Fighter";
        if (s.find("Barbarian") != std::string::npos)  return "Barbarian";
        if (s.find("Rogue") != std::string::npos)      return "Rogue";
        if (s.find("Ranger") != std::string::npos)     return "Ranger";
        if (s.find("Wizard") != std::string::npos)     return "Wizard";
        if (s.find("Cleric") != std::string::npos)     return "Cleric";
        if (s.find("Bard") != std::string::npos)       return "Bard";
        if (s.find("Warlock") != std::string::npos)    return "Warlock";
        if (s.find("Druid") != std::string::npos)      return "Druid";
        return "";
    };

    out.playerClass = "";

    // Primary: CharacterId FString from AccountDataReplication+0x0080
    {
        FString charIdStr = proc.Read<FString>(acctDataAddr + UEOffsets::AcctData_CharacterId);
        if (charIdStr.Data && charIdStr.Count > 0 && charIdStr.Count < 256) {
            std::vector<wchar_t> cidBuf(charIdStr.Count);
            if (proc.ReadRaw(charIdStr.Data, cidBuf.data(), charIdStr.Count * sizeof(wchar_t))) {
                std::string charIdUtf8;
                for (int i = 0; i < charIdStr.Count && cidBuf[i] != 0; i++) {
                    if (cidBuf[i] < 128) charIdUtf8 += static_cast<char>(cidBuf[i]);
                }
                out.playerClass = parseClass(charIdUtf8);
            }
        }
    }

    // Fallback: CharacterKey FName at ADCPlayerCharacterBase+0x0B50
    if (out.playerClass.empty()) {
        FName charKey = proc.Read<FName>(actorAddr + UEOffsets::DCPlayer_CharacterKey);
        std::string keyStr = names.GetFName(proc, charKey.ComparisonIndex, charKey.Number);
        out.playerClass = parseClass(keyStr);
    }

    if (out.playerClass.empty()) out.playerClass = "Player";

    if (out.displayName.empty())
        out.displayName = out.playerClass;

    // Read health via AbilitySystemComponent chain
    ReadCharacterHealth(proc, actorAddr, out);

    // Health is the primary alive/dead indicator.
    // bIsDead/bAlive from AccountData are secondary — their offsets may have shifted.
    // Only override if health says alive but bIsDead says dead (confirmed kill).
    bool bIsDead = proc.Read<bool>(actorAddr + UEOffsets::DCChar_bIsDead);

    static int aliveDiag = 0;
    if (aliveDiag++ % 500 == 0) {
        spdlog::info("[PlayerData] '{}' health={:.0f}/{:.0f} isAlive(hp)={} bIsDead={} bAlive={} bDown={}",
            out.displayName, out.health, out.maxHealth, out.isAlive, bIsDead, bAlive, bDown);
    }

    // Only use bIsDead to force dead — don't use bAlive (offset may be stale)
    if (bIsDead) {
        out.isAlive = false;
    }
    if (bDown) {
        out.displayName += " [DOWN]";
    }
}

void ActorManager::ReadCharacterHealth(const Process& proc, uintptr_t actorAddr,
                                        GameEntity& out) {
    // DCCharacterBase health chain (offsets from 2026 SDK dump):
    // Actor + 0x0708 -> AbilitySystemComponent
    // ASC + 0x1088   -> SpawnedAttributes (TArray<UAttributeSet*>)
    // First attribute (UDCAttributeSet):
    //   + 0x0820 -> Health (FGameplayAttributeData, 0x10 bytes)
    //   + 0x0840 -> MaxHealth (FGameplayAttributeData, 0x10 bytes)
    // FGameplayAttributeData: +0x08 = BaseValue, +0x0C = CurrentValue
    //
    // IMPORTANT: If the pointer chain fails (NULL ASC, empty SpawnedAttrs),
    // we leave isAlive as true (default). Only explicitly set dead if we
    // successfully read health and it's <= 0. This prevents false-dead
    // from race conditions during ability system updates.

    uintptr_t asc = proc.Read<uintptr_t>(actorAddr + UEOffsets::DCChar_AbilitySystemComponent);
    if (!asc) {
        // ASC not found — don't mark dead, just show no health bar
        out.health = -1.0f;  // -1 = unknown (don't show health bar)
        out.maxHealth = 0.0f;
        // Leave isAlive = true (default)
        return;
    }

    TArray<uintptr_t> spawnedAttrs = proc.Read<TArray<uintptr_t>>(asc + UEOffsets::ASC_SpawnedAttributes);
    if (spawnedAttrs.Count <= 0 || !spawnedAttrs.Data) {
        out.health = -1.0f;
        out.maxHealth = 0.0f;
        return;
    }

    // Read first attribute set pointer (UDCAttributeSet)
    uintptr_t attrSet = proc.Read<uintptr_t>(spawnedAttrs.Data);
    if (!attrSet) {
        out.health = -1.0f;
        out.maxHealth = 0.0f;
        return;
    }

    // Cache for fast health reads in render thread
    out.cachedAttrSet = attrSet;

    // Read Health.CurrentValue from UDCAttributeSet
    out.health = proc.Read<float>(attrSet + UEOffsets::Attr_Health + UEOffsets::AttrData_CurrentValue);

    // Read MaxHealth.CurrentValue from UDCAttributeSet
    out.maxHealth = proc.Read<float>(attrSet + UEOffsets::Attr_MaxHealth + UEOffsets::AttrData_CurrentValue);

    // Sanity check
    if (out.maxHealth <= 0.0f) out.maxHealth = 100.0f;

    // Only mark dead if we got a valid read AND health is effectively 0
    // Use epsilon because float reads may not be exactly 0.0
    if (out.health >= 0.0f && out.health < 0.5f) {
        out.isAlive = false;
    }
    // If health > 0.5, leave isAlive = true (default)
    // If health is NaN or negative (garbage), leave isAlive = true (safe default)
}

void ActorManager::ReadItemData(const Process& proc, uintptr_t actorAddr,
                                 GNamesReader& names, GameEntity& out) {
    // Read the actor's FName for basic item identification (fallback)
    FName actorName = proc.Read<FName>(actorAddr + UEOffsets::UObject_Name);
    out.itemName = names.GetFName(proc, actorName.ComparisonIndex, actorName.Number);

    // Determine if this is an ItemHolderActorBase (StaticMeshItemHolder, etc.)
    // vs a specific item BP (BP_FlangedMace_C, BP_Buckler_C, BP_Bandage_C, etc.)
    //
    // ItemHolder actors have valid DataAsset pointers at offset 0x0348.
    // Specific item BPs do NOT inherit from AItemHolderActorBase, so reading
    // 0x0348 gives garbage (e.g., 0x7CF297DA900300D). For these, the class
    // name IS the item identity — we use it directly.
    bool isItemHolder = (out.className.find("ItemHolder") != std::string::npos);

    uintptr_t dataAsset = 0;

    if (isItemHolder) {
        // Path 1: Direct DataAsset pointer at 0x0348 (AItemHolderActorBase)
        dataAsset = proc.Read<uintptr_t>(actorAddr + UEOffsets::ItemHolder_DataAsset);

        // Path 2: Through FDCItemInfo at 0x0350 -> ItemInfo_DataAsset at +0x10
        if (!dataAsset) {
            dataAsset = proc.Read<uintptr_t>(
                actorAddr + UEOffsets::ItemHolder_ItemInfo + UEOffsets::ItemInfo_DataAsset);
        }

        // Validate pointer — game heap is in 0x1XX-0x7EX range, garbage is >0x7F...
        if (dataAsset && (dataAsset > 0x00007FFFFFFFFFFF || dataAsset < 0x10000)) {
            dataAsset = 0; // Invalid pointer
        }
    }
    // For specific item BPs: don't even try DataAsset — it's meaningless at that offset

    static int lootDiag = 0;
    if (lootDiag++ % 500 == 0) {
        spdlog::info("[ItemData] '{}' class='{}' isHolder={} DataAsset=0x{:X}",
            out.itemName, out.className, isItemHolder, dataAsset);
    }

    if (dataAsset) {
        // Read the actual item name from DataAsset's IdTag (FGameplayTag = FName)
        FName idTag = proc.Read<FName>(dataAsset + UEOffsets::ItemData_IdTag);
        std::string idStr = names.GetFName(proc, idTag.ComparisonIndex, idTag.Number);
        if (!idStr.empty()) {
            out.itemName = idStr;
        }

        // Read item type
        uint8_t itemType = proc.Read<uint8_t>(dataAsset + UEOffsets::ItemData_ItemType);
        switch (itemType) {
            case 1: out.itemCategory = "Weapon"; break;
            case 2: out.itemCategory = "Armor"; break;
            case 3: out.itemCategory = "Utility"; break;
            case 4: out.itemCategory = "Accessory"; break;
            case 5: out.itemCategory = "Misc"; break;
            case 6: out.itemCategory = "Gem"; break;
            default: out.itemCategory = "Item"; break;
        }

        // Read gear score
        out.gearScore = proc.Read<int32_t>(dataAsset + UEOffsets::ItemData_GearScore);

        // Read rarity from RarityType GameplayTag
        FName rarityTag = proc.Read<FName>(dataAsset + UEOffsets::ItemData_RarityType);
        std::string rarityStr = names.GetFName(proc, rarityTag.ComparisonIndex, rarityTag.Number);

        // Parse rarity from tag string
        if (!rarityStr.empty()) {
            out.rarity = ParseRarityFromTag(rarityStr);
        } else {
            out.rarity = GuessRarityFromName(out.itemName);
        }
    } else {
        // No valid DataAsset — use class name as item identity
        // For specific BPs like BP_FlangedMace_C, the class name IS the item
        out.rarity = GuessRarityFromName(out.className);
    }

    // Filter out Gold Coins / Gold Pouch — junk info
    if (out.itemName.find("GoldCoin") != std::string::npos ||
        out.itemName.find("Gold_Coin") != std::string::npos ||
        out.itemName.find("GoldPouch") != std::string::npos) {
        out.type = EntityType::UNKNOWN; // Mark for exclusion
        return;
    }

    // Clean up display name — strip common prefixes/suffixes for readability
    std::string name = out.itemName;
    // Strip "DesignDataItem:" or "Item." prefix from gameplay tag names
    size_t colonPos = name.rfind(':');
    if (colonPos != std::string::npos) name = name.substr(colonPos + 1);
    size_t dotPos = name.rfind('.');
    if (dotPos != std::string::npos) name = name.substr(dotPos + 1);
    // Strip BP_ prefix
    if (name.find("BP_") == 0) name = name.substr(3);
    // Strip _C suffix
    if (name.size() > 2 && name.substr(name.size() - 2) == "_C")
        name = name.substr(0, name.size() - 2);
    // Strip instance number suffixes like _2147263766
    if (name.size() > 2) {
        size_t lastUnderscore = name.rfind('_');
        if (lastUnderscore != std::string::npos && lastUnderscore + 1 < name.size()) {
            bool allDigits = true;
            for (size_t i = lastUnderscore + 1; i < name.size(); i++) {
                if (!isdigit(name[i])) { allDigits = false; break; }
            }
            if (allDigits && name.size() - lastUnderscore > 4) // Only strip long number suffixes
                name = name.substr(0, lastUnderscore);
        }
    }
    // Replace underscores with spaces
    for (char& c : name) if (c == '_') c = ' ';

    out.displayName = name;
    if (out.displayName.empty())
        out.displayName = out.itemName.empty() ? out.className : out.itemName;
}

void ActorManager::ReadDeadBodyEquipment(const Process& proc, uintptr_t actorAddr,
                                          GNamesReader& names, GameEntity& out,
                                          ItemRarity minDisplayRarity) {
    // Read inventory component pointer from ADCCharacterBase
    uintptr_t invComp = proc.Read<uintptr_t>(actorAddr + UEOffsets::DCChar_InventoryComponentV2);

    static int bodyDiag = 0;
    bool doDiag = (bodyDiag++ % 300 == 0);

    if (!invComp || invComp > 0x00007FFFFFFFFFFF || invComp < 0x10000)
        return;

    // Read InventoryList TArray<UDCInventoryBase*>
    TArray<uintptr_t> invList = proc.Read<TArray<uintptr_t>>(invComp + UEOffsets::InvContainer_InventoryList);
    if (invList.Count <= 0 || invList.Count > 20 || !invList.Data ||
        invList.Data > 0x00007FFFFFFFFFFF) return;

    // Read all inventory base pointers
    std::vector<uintptr_t> invPtrs(invList.Count);
    if (!proc.ReadRaw(invList.Data, invPtrs.data(), invList.Count * sizeof(uintptr_t))) return;

    // Helper: try reading items with a specific element size and item info offset.
    // Returns true if valid items with real names were found.
    auto tryReadWithLayout = [&](uintptr_t itemsData, int itemsCount,
                                  uintptr_t elemSize, uintptr_t infoOffset,
                                  int invIdx) -> bool {
        bool foundAny = false;
        for (int i = 0; i < itemsCount; i++) {
            uintptr_t elemAddr = itemsData + i * elemSize;
            uintptr_t dataAsset = proc.Read<uintptr_t>(elemAddr + infoOffset + UEOffsets::ItemInfo_DataAsset);
            if (!dataAsset || dataAsset > 0x00007FFFFFFFFFFF || dataAsset < 0x10000) continue;

            // Strong validation: rarity tag must be a real gameplay tag
            FName rarityTag = proc.Read<FName>(dataAsset + UEOffsets::ItemData_RarityType);
            std::string rarityStr = names.GetFName(proc, rarityTag.ComparisonIndex, rarityTag.Number);
            if (rarityStr.find("Rarity.") == std::string::npos) continue;

            // Item name must be valid
            FName itemName = proc.Read<FName>(dataAsset + UEOffsets::ItemData_IdTag);
            std::string name = names.GetFName(proc, itemName.ComparisonIndex, itemName.Number);
            if (name.empty() || name == "None" || name.size() < 5) continue;

            ItemRarity rarity = ParseRarityFromTag(rarityStr);
            foundAny = true;

            if (rarity < minDisplayRarity) continue;

            // Skip Gold Coins / Gold Pouch — user explicitly requested filtering these
            if (name.find("GoldCoin") != std::string::npos ||
                name.find("Gold_Coin") != std::string::npos ||
                name.find("GoldPouch") != std::string::npos) continue;

            // Clean up item name for display
            std::string displayName = name;
            size_t colonPos = displayName.rfind(':');
            if (colonPos != std::string::npos) displayName = displayName.substr(colonPos + 1);
            size_t dotPos = displayName.rfind('.');
            if (dotPos != std::string::npos) displayName = displayName.substr(dotPos + 1);

            out.equipment.push_back(std::string(RarityToString(rarity)) + " " + displayName);
        }
        return foundAny;
    };

    // Helper: try reading items from a TArray at a given offset, trying multiple element layouts.
    auto tryReadItemsAt = [&](uintptr_t invBase, uintptr_t itemsOffset, int invIdx) -> bool {
        TArray<uintptr_t> items = proc.Read<TArray<uintptr_t>>(invBase + itemsOffset);
        if (items.Count <= 0 || items.Count > 50 || !items.Data ||
            items.Data > 0x00007FFFFFFFFFFF || items.Data < 0x10000)
            return false;

        // Layout 1: Runtime-discovered monster loot layout (elem=0x240, info at +0x08)
        if (tryReadWithLayout(items.Data, items.Count, 0x0240, UEOffsets::InvDataElem_ItemInfoAlt, invIdx))
            return true;
        // Layout 2: Standard player equipment layout (elem=0x240, info at +0x10)
        if (tryReadWithLayout(items.Data, items.Count, 0x0240, UEOffsets::InvDataElem_ItemInfo, invIdx))
            return true;
        // Layout 3: info at +0x18 (DataAsset directly at elem+0x18)
        if (tryReadWithLayout(items.Data, items.Count, 0x0240, 0x0018, invIdx))
            return true;
        // Layout 4: info at +0x20
        if (tryReadWithLayout(items.Data, items.Count, 0x0240, 0x0020, invIdx))
            return true;
        // Layout 5: smaller element sizes
        if (tryReadWithLayout(items.Data, items.Count, 0x0238, 0x0010, invIdx))
            return true;
        if (tryReadWithLayout(items.Data, items.Count, 0x0228, 0x0000, invIdx))
            return true;
        return false;
    };

    for (int invIdx = 0; invIdx < static_cast<int>(invPtrs.size()); invIdx++) {
        uintptr_t invBase = invPtrs[invIdx];
        if (!invBase || invBase > 0x00007FFFFFFFFFFF) continue;

        uint8_t invId = proc.Read<uint8_t>(invBase + UEOffsets::InvBase_InventoryId);

        // Try offsets in priority order:
        // 1. Monster loot offset (0xD8) — confirmed working for invId=2 and invId=3
        // 2. Standard offset (0x0238 = InvBase_InventoryData + InvData_Items)
        // 3. Probe nearby offsets if both fail

        // Primary: discovered monster loot offset
        if (tryReadItemsAt(invBase, UEOffsets::InvBase_MonsterItems, invIdx))
            continue;

        // Secondary: standard SDK offset
        uintptr_t stdItemsOffset = UEOffsets::InvBase_InventoryData + UEOffsets::InvData_Items;
        if (tryReadItemsAt(invBase, stdItemsOffset, invIdx))
            continue;

        // Fallback: probe a few offsets near the known ones (minimal — keep scan fast)
        static const uintptr_t fallbackOffsets[] = {
            0x00C0, 0x00C8, 0x00D0, 0x00E0, 0x00E8, 0x00F0,
            0x0100, 0x0110, 0x0120, 0x0128, 0x0140, 0x0150,
            0x0200, 0x0210, 0x0220, 0x0230, 0x0248, 0x0258,
        };
        for (uintptr_t probeOff : fallbackOffsets) {
            if (probeOff == UEOffsets::InvBase_MonsterItems || probeOff == stdItemsOffset)
                continue;
            if (tryReadItemsAt(invBase, probeOff, invIdx)) {
                if (doDiag) {
                    spdlog::info("[DeadBody] Items found at probed offset 0x{:X} for invId={} on '{}'",
                        probeOff, invId, out.displayName);
                }
                break;
            }
        }
    }
}

void ActorManager::ReadChestContents(const Process& proc, uintptr_t actorAddr,
                                      GNamesReader& names, GameEntity& out,
                                      ItemRarity minDisplayRarity) {
    // Chests are ADCInteractableActorBase subclasses — NOT characters.
    // They don't have DCChar_InventoryComponentV2 at 0x0A68.
    // Instead, they have their own inventory component as a UObject* member.
    //
    // Strategy: probe offsets where the inventory component pointer might live.
    // The probing is EXPENSIVE (many relay reads), so we:
    // 1. Cache the discovered offset (same for all chests of same type)
    // 2. Limit to 1 probe attempt per scan cycle (global throttle)
    // 3. After 3 failed probes, give up on probing entirely (chests may not have inventory)

    // Cache the discovered offset across calls
    static uintptr_t cachedChestInvOffset = 0;
    static bool probeComplete = false;
    static int probeFailCount = 0;

    // Helper: try reading items from an inventory base with multiple layouts.
    auto tryReadItemsFromInvBase = [&](uintptr_t invBase) -> bool {
        if (!invBase || invBase > 0x00007FFFFFFFFFFF) return false;

        // Item offsets to try within UDCInventoryBase:
        static const uintptr_t itemOffsets[] = {
            UEOffsets::InvBase_MonsterItems,  // 0xD8 — discovered working offset
            UEOffsets::InvBase_InventoryData + UEOffsets::InvData_Items,  // 0x238 — standard SDK
        };

        // Element layouts to try (elemSize, infoOffset)
        struct ElemLayout { uintptr_t size; uintptr_t info; };
        static const ElemLayout layouts[] = {
            { 0x0240, UEOffsets::InvDataElem_ItemInfoAlt },  // 0x08 — monster layout
            { 0x0240, UEOffsets::InvDataElem_ItemInfo },     // 0x10 — standard layout
            { 0x0240, 0x0018 },
        };

        for (uintptr_t itemsOff : itemOffsets) {
            TArray<uintptr_t> items = proc.Read<TArray<uintptr_t>>(invBase + itemsOff);
            if (items.Count <= 0 || items.Count > 50 || !items.Data ||
                items.Data > 0x00007FFFFFFFFFFF || items.Data < 0x10000)
                continue;

            for (const auto& layout : layouts) {
                bool foundAny = false;
                for (int i = 0; i < items.Count; i++) {
                    uintptr_t elemAddr = items.Data + i * layout.size;
                    uintptr_t dataAsset = proc.Read<uintptr_t>(
                        elemAddr + layout.info + UEOffsets::ItemInfo_DataAsset);
                    if (!dataAsset || dataAsset > 0x00007FFFFFFFFFFF || dataAsset < 0x10000) continue;

                    FName rarityTag = proc.Read<FName>(dataAsset + UEOffsets::ItemData_RarityType);
                    std::string rarityStr = names.GetFName(proc, rarityTag.ComparisonIndex, rarityTag.Number);
                    if (rarityStr.find("Rarity.") == std::string::npos) continue;

                    FName itemName = proc.Read<FName>(dataAsset + UEOffsets::ItemData_IdTag);
                    std::string name = names.GetFName(proc, itemName.ComparisonIndex, itemName.Number);
                    if (name.empty() || name == "None" || name.size() < 5) continue;

                    ItemRarity rarity = ParseRarityFromTag(rarityStr);
                    foundAny = true;

                    if (rarity < minDisplayRarity) continue;
                    if (name.find("GoldCoin") != std::string::npos ||
                        name.find("GoldPouch") != std::string::npos) continue;

                    // Clean up item name
                    std::string displayName = name;
                    size_t colonPos = displayName.rfind(':');
                    if (colonPos != std::string::npos) displayName = displayName.substr(colonPos + 1);
                    size_t dotPos = displayName.rfind('.');
                    if (dotPos != std::string::npos) displayName = displayName.substr(dotPos + 1);

                    out.equipment.push_back(std::string(RarityToString(rarity)) + " " + displayName);
                }
                if (foundAny) return true;
            }
        }
        return false;
    };

    auto tryReadInventory = [&](uintptr_t invComp) -> bool {
        if (!invComp || invComp > 0x00007FFFFFFFFFFF || invComp < 0x10000)
            return false;

        TArray<uintptr_t> invList = proc.Read<TArray<uintptr_t>>(invComp + UEOffsets::InvContainer_InventoryList);
        if (invList.Count <= 0 || invList.Count > 20 || !invList.Data ||
            invList.Data > 0x00007FFFFFFFFFFF)
            return false;

        std::vector<uintptr_t> invPtrs(invList.Count);
        if (!proc.ReadRaw(invList.Data, invPtrs.data(), invList.Count * sizeof(uintptr_t)))
            return false;

        bool foundAnyItem = false;
        for (uintptr_t invBase : invPtrs) {
            if (tryReadItemsFromInvBase(invBase))
                foundAnyItem = true;
        }
        return foundAnyItem;
    };

    // Fast path: use cached offset if already found
    if (probeComplete && cachedChestInvOffset != 0) {
        uintptr_t invComp = proc.Read<uintptr_t>(actorAddr + cachedChestInvOffset);
        tryReadInventory(invComp);
        return;
    }

    // Give up after 3 failed full probes — chests likely don't expose inventory this way
    if (probeFailCount >= 3) return;

    // Throttle: only probe once every ~5 seconds to avoid killing FPS
    static auto lastProbeTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastProbeTime).count() < 5) return;
    lastProbeTime = now;

    // Slow path: probe offsets to find inventory component
    // Use a smaller range with wider stride to reduce relay reads
    for (uintptr_t offset = 0x0300; offset <= 0x0600; offset += 0x08) {
        uintptr_t candidate = proc.Read<uintptr_t>(actorAddr + offset);
        if (tryReadInventory(candidate)) {
            cachedChestInvOffset = offset;
            probeComplete = true;
            probeFailCount = 0;
            spdlog::info("[ChestProbe] Found chest inventory at offset 0x{:X} on '{}'",
                offset, out.className);
            return;
        }
    }

    probeFailCount++;
    spdlog::info("[ChestProbe] No inventory for '{}' (attempt {}/3)", out.className, probeFailCount);
}

void ActorManager::ReadMonsterData(const Process& proc, uintptr_t actorAddr,
                                    GNamesReader& names, GameEntity& out) {
    // Read health first
    ReadCharacterHealth(proc, actorAddr, out);

    // Fallback: if health chain failed (health=-1), check bIsDead flag directly.
    // Some monsters (e.g. SkeletonGuardmanShield) don't have valid ASC/SpawnedAttributes
    // but DO have bIsDead=true when killed. Without this fallback, dead monsters show
    // as "alive" and their loot inventory is never read.
    if (out.health < 0.0f) {
        bool bIsDead = proc.Read<bool>(actorAddr + UEOffsets::DCChar_bIsDead);
        if (bIsDead) {
            out.isAlive = false;
            out.health = 0.0f;
        }
    }

    // FromFakeDeath actors are the "waking up" version of skeletons.
    // They spawn as a separate actor during the fake-death animation.
    // Force them to show as alive (they're literally standing up to attack you).
    if (out.className.find("FromFakeDeath") != std::string::npos) {
        out.isAlive = true;
    }

    static int monsterDiag = 0;
    if (monsterDiag++ % 500 == 0) {
        spdlog::info("[MonsterData] '{}' health={:.0f}/{:.0f} isAlive={} addr=0x{:X}",
            out.className, out.health, out.maxHealth, out.isAlive, actorAddr);
    }

    // Clean up display name from class name (strip BP_ prefix and _C/_Common suffixes)
    std::string name = out.className;
    if (name.find("BP_") == 0) name = name.substr(3);
    // Remove trailing _C
    if (name.size() > 2 && name.substr(name.size() - 2) == "_C")
        name = name.substr(0, name.size() - 2);
    // Remove _Common, _Soulflame, _Nightmare suffixes (grade is shown separately)
    auto stripSuffix = [&](const char* suffix) {
        size_t pos = name.rfind(suffix);
        if (pos != std::string::npos && pos + strlen(suffix) == name.size())
            name = name.substr(0, pos);
    };
    stripSuffix("_Common");
    stripSuffix("_Soulflame");
    stripSuffix("_Nightmare");
    stripSuffix("FromFakeDeath");
    // Replace underscores with spaces for readability
    for (char& c : name) if (c == '_') c = ' ';
    out.displayName = name;

    // Read monster grade from DesignDataAssetMonster -> FDesignDataMonster -> GradeType
    uintptr_t designAsset = proc.Read<uintptr_t>(actorAddr + UEOffsets::DCMonster_DesignDataAssetMonster);
    if (designAsset) {
        // GradeType is a FGameplayTag (just an FName inside)
        FName gradeTag = proc.Read<FName>(
            designAsset + UEOffsets::MonsterDataAsset_Item + UEOffsets::DesignMonster_GradeType);
        std::string gradeStr = names.GetFName(proc, gradeTag.ComparisonIndex, gradeTag.Number);

        // Parse grade from tag string (e.g., "Monster.Grade.Elite")
        std::string lower = gradeStr;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("nightmare") != std::string::npos)
            out.monsterGrade = MonsterGrade::Nightmare;
        else if (lower.find("elite") != std::string::npos)
            out.monsterGrade = MonsterGrade::Elite;
        else if (lower.find("common") != std::string::npos)
            out.monsterGrade = MonsterGrade::Common;
    }
}

// ============================================================================
//  Classification — Dark and Darker LIVE class names (Feb 2026)
//  Source: Runtime actor census from Goblin Caves / Ruins dungeon
//  264 unique classes identified across 26,598 actors
//
//  Key patterns (all end with _C suffix for Blueprint classes):
//    BP_PlayerCharacterDungeon_C — players in dungeon
//    BP_Skeleton*_Common_C, BP_Mummy_Common_C, etc. — monsters
//    BP_*Chest*_N0_C — chests (N0 = tier 0?)
//    BP_StaticMeshItemHolder_C — generic ground items
//    BP_*Sword_C, BP_*Shield_C, BP_*Potion_C — specific item BPs
// ============================================================================
EntityType ActorManager::ClassifyByName(const std::string& className) {
    // ---------- EARLY REJECT ----------
    // GameplayCue / VFX actors (GC_ prefix) — visual effects, not real entities
    if (className.find("GC_") == 0) {
        return EntityType::UNKNOWN;
    }
    // Projectiles — arrows, magic missiles, thrown objects
    if (className.find("_Arrow_C") != std::string::npos ||
        className.find("_MagicArrow_C") != std::string::npos ||
        className.find("_Missile_C") != std::string::npos ||
        className.find("_Projectile_C") != std::string::npos ||
        className.find("Thrown") != std::string::npos) {
        return EntityType::UNKNOWN;
    }
    // AoE effects, ability actors, poison zones — not real entities
    if (className.find("_Aoe_") != std::string::npos ||
        className.find("PoisonArea") != std::string::npos ||
        className.find("PoisonSting") != std::string::npos ||
        className.find("_Area_C") != std::string::npos ||
        className.find("_Effect_C") != std::string::npos ||
        className.find("Decal") != std::string::npos) {
        return EntityType::UNKNOWN;
    }

    // ---------- Players ----------
    // BP_PlayerCharacterDungeon_C — normal player pawns
    // Druid shapeshift forms spawn as separate actors with different class names
    if (className.find("BP_PlayerCharacter") == 0) {
        return EntityType::PLAYER;
    }
    // Druid shapeshift forms — spawn as separate actor pawns, NOT BP_PlayerCharacter.
    // Patterns: "ShapeShift", "DruidBear", "BearForm", etc.
    // NOTE: Must NOT match actual animal mobs (BP_Bear_Common_C, BP_DireWolf_Common_C).
    // If the exact class name is unknown, add a diagnostic in the "cache miss" path below
    // to log any actor near a player that has "Bear"/"Druid"/"Shift" in its name.
    if (className.find("ShapeShift") != std::string::npos ||
        className.find("DruidBear") != std::string::npos ||
        className.find("DruidPanther") != std::string::npos ||
        className.find("DruidChicken") != std::string::npos ||
        className.find("DruidRat") != std::string::npos ||
        className.find("BearForm") != std::string::npos ||
        className.find("PantherForm") != std::string::npos) {
        return EntityType::PLAYER;
    }

    // ---------- Special / Rare Chests only ----------
    // Gold, Marvelous, Ornate — the big ones worth showing
    if (className.find("GoldChest") != std::string::npos ||
        className.find("MarvelousChest") != std::string::npos ||
        className.find("OrnateChest") != std::string::npos) {
        return EntityType::CHEST_SPECIAL;
    }

    // ---------- Monsters / NPCs ----------
    // Must start with BP_ to avoid GC_ (GameplayCue) VFX actors
    if (className.find("BP_") != 0) {
        // Not a BP_ actor — skip monster/loot checks entirely
        return EntityType::UNKNOWN;
    }

    // Skeleton variants (exclude corpse/bones decorations)
    if (className.find("Skeleton") != std::string::npos &&
        (className.find("Swordman") != std::string::npos ||
         className.find("Archer") != std::string::npos ||
         className.find("Footman") != std::string::npos ||
         className.find("Guardman") != std::string::npos ||
         className.find("Champion") != std::string::npos)) {
        return EntityType::MONSTER;
    }
    // Mummy (not SpiderMummy, not corpse props)
    if (className.find("Mummy") != std::string::npos &&
        className.find("Spider") == std::string::npos &&
        className.find("Corpse") == std::string::npos) {
        return EntityType::MONSTER;
    }
    // Named monster types
    if (className.find("SpiderMummy") != std::string::npos ||
        className.find("Cockatrice") != std::string::npos ||
        className.find("Banshee") != std::string::npos ||
        className.find("SpectralKnight") != std::string::npos ||
        className.find("DeathSkull") != std::string::npos ||
        className.find("LivingArmor") != std::string::npos ||
        className.find("DireWolf") != std::string::npos ||
        className.find("Wisp") != std::string::npos ||
        className.find("GiantDragonfly") != std::string::npos ||
        className.find("Zombie") != std::string::npos ||
        className.find("Gargoyle") != std::string::npos ||
        className.find("GiantBat") != std::string::npos ||
        className.find("GiantSpider") != std::string::npos ||
        className.find("GiantCentipede") != std::string::npos ||
        className.find("Wraith") != std::string::npos ||
        className.find("GhostKing") != std::string::npos ||
        className.find("Lich") != std::string::npos ||
        className.find("Mimic") != std::string::npos ||
        className.find("Wyvern") != std::string::npos ||
        className.find("Troll") != std::string::npos ||
        className.find("Goblin") != std::string::npos ||
        className.find("Wolf") != std::string::npos ||
        className.find("Bear") != std::string::npos ||
        className.find("LivingStatue") != std::string::npos ||
        className.find("Living_Statue") != std::string::npos ||
        className.find("Demon") != std::string::npos ||
        className.find("DemonDog") != std::string::npos) {
        return EntityType::MONSTER;
    }
    // Generic monster suffix patterns (BP_*_Common_C, BP_*_Soulflame_C, etc.)
    if (className.find("_Common_C") != std::string::npos ||
        className.find("_Soulflame_C") != std::string::npos ||
        className.find("_Nightmare_C") != std::string::npos) {
        return EntityType::MONSTER;
    }

    // ---------- Loot Items (ground drops) ----------
    // ItemHolder = generic container for ground items
    if (className.find("ItemHolder") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }

    // Specific item BPs (player drops, world spawns, etc.)
    // Weapons
    if (className.find("Sword_C") != std::string::npos ||
        className.find("Axe_C") != std::string::npos ||
        className.find("Mace_C") != std::string::npos ||
        className.find("Dagger_C") != std::string::npos ||
        className.find("Rapier_C") != std::string::npos ||
        className.find("Halberd_C") != std::string::npos ||
        className.find("Spear_C") != std::string::npos ||
        className.find("Longsword_C") != std::string::npos ||
        className.find("Falchion_C") != std::string::npos ||
        className.find("Bardiche_C") != std::string::npos ||
        className.find("Zweihander_C") != std::string::npos ||
        className.find("Quarterstaff_C") != std::string::npos ||
        className.find("HandCannon_C") != std::string::npos ||
        className.find("Crossbow_C") != std::string::npos ||
        className.find("Longbow_C") != std::string::npos ||
        className.find("RecurveBow_C") != std::string::npos ||
        className.find("Spellbook_C") != std::string::npos ||
        className.find("Staff_C") != std::string::npos ||
        className.find("CrystalBall_C") != std::string::npos ||
        className.find("Wand_C") != std::string::npos ||
        className.find("Flute_C") != std::string::npos ||
        className.find("Lute_C") != std::string::npos ||
        className.find("Drum_C") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }
    // Shields
    if (className.find("Buckler_C") != std::string::npos ||
        className.find("HeaterShield_C") != std::string::npos ||
        className.find("RoundShield_C") != std::string::npos ||
        className.find("Pavise_C") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }
    // Armor pieces (generic patterns)
    if (className.find("Helmet_C") != std::string::npos ||
        className.find("Chestplate_C") != std::string::npos ||
        className.find("Leggings_C") != std::string::npos ||
        className.find("Gloves_C") != std::string::npos ||
        className.find("Boots_C") != std::string::npos ||
        className.find("Cloak_C") != std::string::npos ||
        className.find("Hood_C") != std::string::npos ||
        className.find("Hat_C") != std::string::npos ||
        className.find("Robe_C") != std::string::npos ||
        className.find("Tunic_C") != std::string::npos ||
        className.find("Cape_C") != std::string::npos ||
        className.find("Gauntlet_C") != std::string::npos ||
        className.find("Necklace_C") != std::string::npos ||
        className.find("Pendant_C") != std::string::npos ||
        className.find("Ring_C") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }
    // Consumables & utility
    if (className.find("HealingPotion") != std::string::npos ||
        className.find("ProtectionPotion") != std::string::npos ||
        className.find("InvisibilityPotion") != std::string::npos ||
        className.find("Potion_C") != std::string::npos ||
        className.find("Bandage_C") != std::string::npos ||
        className.find("Lantern_C") != std::string::npos ||
        className.find("Torch_C") != std::string::npos ||
        className.find("Ale_C") != std::string::npos ||
        className.find("CampfireKit_C") != std::string::npos ||
        className.find("Scroll_C") != std::string::npos ||
        className.find("Trap_C") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }
    // Gold Coins / Gold Pouch — user explicitly requested filtering these out (junk info)
    if (className.find("GoldCoin") != std::string::npos ||
        className.find("GoldPouch") != std::string::npos ||
        className.find("Gold_Coin") != std::string::npos) {
        return EntityType::UNKNOWN;  // Skip — junk
    }
    // Gems, valuables & misc loot
    if (className.find("Gem_C") != std::string::npos ||
        className.find("Coin_C") != std::string::npos ||
        className.find("Ingot_C") != std::string::npos ||
        className.find("Ore_C") != std::string::npos ||
        className.find("Trophy_C") != std::string::npos) {
        return EntityType::LOOT_ITEM;
    }

    // ---------- Portals (escape, down, floor portals) ----------
    if (className.find("FloorPortal") != std::string::npos ||
        className.find("EscapePortal") != std::string::npos ||
        className.find("DownPortal") != std::string::npos ||
        className.find("UpPortal") != std::string::npos ||
        className.find("Portal_C") != std::string::npos ||
        className.find("Escape_C") != std::string::npos ||
        className.find("ExtractionPortal") != std::string::npos) {
        return EntityType::PORTAL;
    }

    return EntityType::UNKNOWN;
}

ItemRarity ActorManager::ParseRarityFromTag(const std::string& tagStr) {
    // FGameplayTag rarity strings from SDK:
    // "Item.Rarity.Poor", "Item.Rarity.Common", etc.
    std::string lower = tagStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("artifact") != std::string::npos) return ItemRarity::ARTIFACT;
    if (lower.find("unique") != std::string::npos)   return ItemRarity::UNIQUE;
    if (lower.find("legend") != std::string::npos)   return ItemRarity::LEGENDARY;
    if (lower.find("epic") != std::string::npos)     return ItemRarity::EPIC;
    if (lower.find("rare") != std::string::npos)     return ItemRarity::RARE;
    if (lower.find("uncommon") != std::string::npos) return ItemRarity::UNCOMMON;
    if (lower.find("common") != std::string::npos)   return ItemRarity::COMMON;
    if (lower.find("poor") != std::string::npos)     return ItemRarity::POOR;

    return ItemRarity::COMMON;
}

ItemRarity ActorManager::GuessRarityFromName(const std::string& name) {
    // Fallback: keyword-based heuristic from actor/class name
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("artifact") != std::string::npos) return ItemRarity::ARTIFACT;
    if (lower.find("unique") != std::string::npos)   return ItemRarity::UNIQUE;
    if (lower.find("legendary") != std::string::npos || lower.find("legend") != std::string::npos)
        return ItemRarity::LEGENDARY;
    if (lower.find("epic") != std::string::npos)     return ItemRarity::EPIC;
    if (lower.find("rare") != std::string::npos)     return ItemRarity::RARE;
    if (lower.find("uncommon") != std::string::npos) return ItemRarity::UNCOMMON;
    if (lower.find("common") != std::string::npos)   return ItemRarity::COMMON;
    if (lower.find("poor") != std::string::npos)     return ItemRarity::POOR;

    return ItemRarity::COMMON;
}
