#pragma once
#include <cstdint>
#include <cmath>

// UE5 uses double-precision vectors (changed from float in UE4)
struct FVector {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;

    FVector operator-(const FVector& other) const {
        return {X - other.X, Y - other.Y, Z - other.Z};
    }

    FVector operator+(const FVector& other) const {
        return {X + other.X, Y + other.Y, Z + other.Z};
    }

    FVector operator*(double scalar) const {
        return {X * scalar, Y * scalar, Z * scalar};
    }

    double Length() const {
        return std::sqrt(X * X + Y * Y + Z * Z);
    }

    double DistanceTo(const FVector& other) const {
        return (*this - other).Length();
    }

    // Distance in meters (UE uses centimeters internally)
    double DistanceToMeters(const FVector& other) const {
        return DistanceTo(other) / 100.0;
    }
};

struct FVector2D {
    float X = 0.0f;
    float Y = 0.0f;
};

struct FRotator {
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
};

struct FTransform {
    // Quaternion rotation
    double RotationX = 0.0, RotationY = 0.0, RotationZ = 0.0, RotationW = 1.0;
    // Translation
    double TranslationX = 0.0, TranslationY = 0.0, TranslationZ = 0.0;
    double TranslationPad = 0.0; // Padding
    // Scale
    double ScaleX = 1.0, ScaleY = 1.0, ScaleZ = 1.0;
    double ScalePad = 0.0; // Padding
};

template<typename T>
struct TArray {
    uintptr_t Data = 0;   // Pointer to contiguous array
    int32_t Count = 0;     // Current element count
    int32_t Max = 0;       // Allocated capacity
};

struct FName {
    int32_t ComparisonIndex = 0;  // Index into GNames pool
    int32_t Number = 0;           // Instance number (0 = no suffix)
};

struct FMinimalViewInfo {
    FVector Location;    // 0x00 (24 bytes)
    FRotator Rotation;   // 0x18 (24 bytes)
    float FOV;           // 0x30
};

// D3D/Math matrix for world-to-screen
struct FMatrix {
    float M[4][4] = {};

    FMatrix operator*(const FMatrix& other) const {
        FMatrix result;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                result.M[i][j] = 0;
                for (int k = 0; k < 4; k++)
                    result.M[i][j] += M[i][k] * other.M[k][j];
            }
        return result;
    }
};

// UObject base layout — offsets from Dumper-7 SDK dump (v5.5.4, Feb 2026)
// Source: C:\Dumper-7\5.5.4-0+UE5-DungeonCrawler\CppSDK\
// NOTE: These may shift between game patches. If actors stop loading,
// re-deploy proxy + Dumper-7 to regenerate SDK dump and update offsets.
namespace UEOffsets {
    // ========================
    //  UObject (base class, 0x28 bytes)
    // ========================
    constexpr uintptr_t UObject_VTable      = 0x00;
    constexpr uintptr_t UObject_Flags       = 0x08;
    constexpr uintptr_t UObject_Index       = 0x0C;
    constexpr uintptr_t UObject_Class       = 0x10;   // UClass*
    constexpr uintptr_t UObject_Name        = 0x18;   // FName
    constexpr uintptr_t UObject_Outer       = 0x20;

    // ========================
    //  AActor (0x02A8 total, inherits UObject)
    //  Source: Engine_classes.hpp
    // ========================
    constexpr uintptr_t AActor_bHidden           = 0x0058;  // bit 7
    constexpr uintptr_t AActor_bBeingDestroyed   = 0x005D;  // bit 0
    constexpr uintptr_t AActor_Owner             = 0x0158;  // AActor*
    constexpr uintptr_t AActor_RootComponent     = 0x01B8;  // USceneComponent*

    // ========================
    //  USceneComponent
    //  Source: Engine_classes.hpp
    // ========================
    constexpr uintptr_t SceneComp_RelativeLocation = 0x0128;  // FVector (24 bytes)
    constexpr uintptr_t SceneComp_RelativeRotation = 0x0140;  // FRotator
    constexpr uintptr_t SceneComp_ComponentVelocity = 0x0170; // FVector
    constexpr uintptr_t SceneComp_ComponentToWorld = 0x01C0;  // FTransform

    // ========================
    //  UWorld (0x0908 total)
    //  Source: Engine_classes.hpp line 12701
    // ========================
    constexpr uintptr_t UWorld_PersistentLevel    = 0x0030;  // ULevel*
    constexpr uintptr_t UWorld_GameState          = 0x0160;  // AGameStateBase*
    constexpr uintptr_t UWorld_Levels             = 0x0178;  // TArray<ULevel*>
    constexpr uintptr_t UWorld_OwningGameInstance = 0x01D8;  // UGameInstance*

    // ========================
    //  ULevel (inherits UObject)
    //  Source: Engine_classes.hpp line 8836
    //  Actors array — auto-detected at 0xA0 (was 0x98 in older builds)
    //  Verified: 25501 actors across 148 levels in Goblin Caves dungeon
    // ========================
    constexpr uintptr_t ULevel_Actors = 0xA0;   // TArray<AActor*> (hidden in padding)

    // ========================
    //  UGameInstance -> ULocalPlayer -> APlayerController chain
    //  Source: Engine_classes.hpp
    // ========================
    constexpr uintptr_t GameInstance_LocalPlayers           = 0x0038;  // TArray<ULocalPlayer*>
    constexpr uintptr_t LocalPlayer_PlayerController       = 0x0030;  // APlayerController*
    constexpr uintptr_t PlayerController_AcknowledgedPawn  = 0x0350;  // APawn*
    constexpr uintptr_t PlayerController_PlayerCameraManager = 0x0360; // APlayerCameraManager*

    // ========================
    //  APlayerCameraManager
    //  Source: Engine_classes.hpp line 26586
    // ========================
    constexpr uintptr_t CameraManager_CameraCachePrivate = 0x1410;  // FCameraCacheEntry
    constexpr uintptr_t CameraCache_POV = 0x10;  // FMinimalViewInfo (0x830 bytes)

    // ========================
    //  Dark and Darker: ADCCharacterBase (0x0AE0 total)
    //  Source: DungeonCrawler_classes.hpp
    //  Inherits: ACharacter (0x0650) → ADCCharacterBase
    // ========================
    constexpr uintptr_t DCChar_AbilitySystemComponent = 0x0708;  // UAbilitySystemComponent*
    constexpr uintptr_t DCChar_GenericTeamId          = 0x0720;  // FGenericTeamId
    constexpr uintptr_t DCChar_AccountId              = 0x07D8;  // FString
    constexpr uintptr_t DCChar_AccountDataReplication = 0x07F8;  // FAccountDataReplication (0xC0)
    constexpr uintptr_t DCChar_bIsDead                = 0x08B9;  // bool
    constexpr uintptr_t DCChar_InventoryComponentV2   = 0x0A68;  // UDCInventoryComponentV2*

    // ========================
    //  FAccountDataReplication (0x00C0 total)
    //  Source: DungeonCrawler_structs.hpp line 5032
    //  Read from: actorAddr + DCChar_AccountDataReplication + offset
    // ========================
    constexpr uintptr_t AcctData_AccountId   = 0x0000;  // FString
    constexpr uintptr_t AcctData_Nickname    = 0x0010;  // FNickname (0x70)
    constexpr uintptr_t AcctData_CharacterId = 0x0080;  // FString (class ID like "DesignDataPlayerCharacter:Id_PlayerCharacter_Fighter")
                                                         // Was 0x00A0 in older builds, confirmed shifted to 0x0080 via runtime scan (Feb 2026)
    constexpr uintptr_t AcctData_Gender      = 0x00B0;  // int32
    constexpr uintptr_t AcctData_Level       = 0x00B4;  // int32
    constexpr uintptr_t AcctData_bAlive      = 0x00BB;  // bool
    constexpr uintptr_t AcctData_bDown       = 0x00BD;  // bool

    // ========================
    //  FNickname (0x70 total)
    //  Source: DungeonCrawler_structs.hpp line 4231
    // ========================
    constexpr uintptr_t Nickname_OriginalNickName = 0x0000;  // FString (the player's display name)

    // ========================
    //  ADCPlayerCharacterBase (0x0D10 total, inherits ADCCharacterBase)
    //  Source: DungeonCrawler_classes.hpp
    // ========================
    constexpr uintptr_t DCPlayer_CharacterKey         = 0x0B50;  // FName or struct
    constexpr uintptr_t DCPlayer_DataComponent        = 0x0B80;
    constexpr uintptr_t DCPlayer_EquipmentComponentV2 = 0x0B98;

    // ========================
    //  UAbilitySystemComponent -> UDCAttributeSet health chain
    //  Source: GameplayAbilities_classes.hpp, DungeonCrawler_classes.hpp
    // ========================
    constexpr uintptr_t ASC_SpawnedAttributes = 0x1088;  // TArray<UAttributeSet*>

    // ========================
    //  UDCAttributeSet (0x0C78 total, inherits UDCAttributeSetBase at 0x30)
    //  Source: DungeonCrawler_classes.hpp line ~21050
    //  FGameplayAttributeData is 0x10 bytes: [pad 0x8][BaseValue float][CurrentValue float]
    // ========================
    constexpr uintptr_t Attr_Strength        = 0x0030;  // FGameplayAttributeData
    constexpr uintptr_t Attr_Vigor           = 0x0060;
    constexpr uintptr_t Attr_Agility         = 0x0090;
    constexpr uintptr_t Attr_RecoverableHealth = 0x0810;
    constexpr uintptr_t Attr_Health          = 0x0820;  // FGameplayAttributeData
    constexpr uintptr_t Attr_OverhealedHealth = 0x0830;
    constexpr uintptr_t Attr_MaxHealth       = 0x0840;  // FGameplayAttributeData
    // FGameplayAttributeData layout: +0x08 = BaseValue (float), +0x0C = CurrentValue (float)
    constexpr uintptr_t AttrData_BaseValue    = 0x08;
    constexpr uintptr_t AttrData_CurrentValue = 0x0C;

    // ========================
    //  UDCMonsterDmgIndicatorComponent (simple health)
    //  Source: DungeonCrawler_classes.hpp
    // ========================
    constexpr uintptr_t MonsterDmg_Health    = 0x00B4;  // float
    constexpr uintptr_t MonsterDmg_MaxHealth = 0x00B8;  // float

    // ========================
    //  AItemHolderActorBase (0x06B0 total, items on ground)
    //  Source: DungeonCrawler_classes.hpp line 18897
    //  Inherits: ADCInteractableActorBase (0x0318)
    // ========================
    constexpr uintptr_t ItemHolder_MeshComponent = 0x0330;  // UMeshComponent*
    constexpr uintptr_t ItemHolder_ItemId        = 0x0338;  // FPrimaryAssetId (0x10)
    constexpr uintptr_t ItemHolder_DataAsset     = 0x0348;  // UDCItemDataAsset*
    constexpr uintptr_t ItemHolder_ItemInfo      = 0x0350;  // FDCItemInfo (0x228)

    // ========================
    //  UDCItemDataAsset (0x270 total)
    //  Source: DungeonCrawler_classes.hpp line 34117
    // ========================
    constexpr uintptr_t ItemData_IdTag      = 0x0080;  // FGameplayTag
    constexpr uintptr_t ItemData_Name       = 0x00B0;  // FText
    constexpr uintptr_t ItemData_ItemType   = 0x00D0;  // EItemType (uint8)
    constexpr uintptr_t ItemData_RarityType = 0x0118;  // FGameplayTag
    constexpr uintptr_t ItemData_GearScore  = 0x0268;  // int32

    // ========================
    //  FDCItemInfo (0x228 total, carried/ground item state)
    //  Source: DungeonCrawler_structs.hpp line 4653
    // ========================
    constexpr uintptr_t ItemInfo_ID        = 0x0008;  // FDCItemId (0x08)
    constexpr uintptr_t ItemInfo_DataAsset = 0x0010;  // UDCItemDataAsset*
    constexpr uintptr_t ItemInfo_Stack     = 0x0028;  // int32

    // ========================
    //  UDCInventoryComponent (character inventory system)
    //  Source: DungeonCrawler_classes.hpp
    //  Inherits: UDCInventoryContainerComponent (0x0170 base)
    // ========================
    constexpr uintptr_t InvContainer_InventoryList = 0x0170;  // TArray<UDCInventoryBase*>

    // ========================
    //  UDCInventoryBase (inventory slot container)
    //  Source: DungeonCrawler_classes.hpp
    // ========================
    constexpr uintptr_t InvBase_InventoryId     = 0x0070;  // EDCInventoryId (uint8)
    constexpr uintptr_t InvBase_InventoryData   = 0x0130;  // FDCInventoryData

    // ========================
    //  FDCInventoryData -> fast array of items
    //  Source: DungeonCrawler_structs.hpp
    //  Contains FFastArraySerializer header then TArray<FDCInventoryDataElement>
    // ========================
    constexpr uintptr_t InvData_Items           = 0x0108;  // TArray<FDCInventoryDataElement> within FDCInventoryData

    // ========================
    //  FDCInventoryDataElement (one inventory slot)
    //  Source: DungeonCrawler_structs.hpp
    // ========================
    constexpr uintptr_t InvDataElem_Index       = 0x000C;  // int32 (slot index)
    constexpr uintptr_t InvDataElem_ItemInfo    = 0x0010;  // FDCItemInfo (0x228)
    constexpr uintptr_t InvDataElem_Size        = 0x0240;  // Total size per element

    // ========================
    //  Monster loot TArray offset (runtime-discovered, Feb 2026)
    //  Monster bodies use invId=2. The items TArray lives at invBase+0xD8
    //  (NOT at the standard InvBase_InventoryData + InvData_Items = 0x0238).
    //  Element layout: size=0x240, DataAsset at elem+0x08+ItemInfo_DataAsset(0x10)=elem+0x18.
    //  Confirmed: DeathSkull(Arrow), SkeletonArcher(IntactSkull), RuinsGolem(Armet), dead Players.
    // ========================
    constexpr uintptr_t InvBase_MonsterItems    = 0x00D8;  // TArray<FDCInventoryDataElement> (monster invId=2)
    constexpr uintptr_t InvDataElem_ItemInfoAlt = 0x0008;  // FDCItemInfo offset in monster loot elements

    // EDCInventoryId values
    constexpr uint8_t INVENTORY_ID_EQUIPMENT    = 3;

    // ========================
    //  ADCMonsterBase (inherits ADCCharacterBase at 0x0AE0)
    //  Source: DungeonCrawler_classes.hpp line 4942
    // ========================
    constexpr uintptr_t DCMonster_MonsterId              = 0x0BC8;  // FPrimaryAssetId
    constexpr uintptr_t DCMonster_DesignDataAssetMonster = 0x0DB8;  // UDesignDataAssetMonster*

    // ========================
    //  UDesignDataAssetMonster -> FDesignDataMonster
    //  Source: DungeonCrawler_classes.hpp line 16312, DungeonCrawler_structs.hpp line 11178
    // ========================
    constexpr uintptr_t MonsterDataAsset_Item   = 0x0070;  // FDesignDataMonster struct
    constexpr uintptr_t DesignMonster_IdTag     = 0x0000;  // FGameplayTag
    constexpr uintptr_t DesignMonster_ClassType = 0x0008;  // FGameplayTag
    constexpr uintptr_t DesignMonster_GradeType = 0x0010;  // FGameplayTag (Common/Elite/Nightmare)
    constexpr uintptr_t DesignMonster_Name      = 0x0028;  // FText

    // ========================
    //  ACharacter -> USkeletalMeshComponent (bone-based ESP)
    //  Source: Engine_classes.hpp line 30008 (ACharacter::Mesh at 0x0328)
    //  Source: Engine_classes.hpp line 5789 (USkeletalMeshComponent)
    // ========================
    constexpr uintptr_t ACharacter_Mesh = 0x0328;  // USkeletalMeshComponent*

    // USkeletalMeshComponent bone transform arrays
    constexpr uintptr_t SkelMesh_CachedComponentSpaceTransforms = 0x0960; // TArray<FTransform>

    // FTransform is 0x60 bytes (96 bytes) per bone
    constexpr size_t FTransform_Size = 0x60;
    // Translation offset within FTransform (after quaternion rotation: 4 doubles = 32 bytes)
    constexpr uintptr_t FTransform_Translation = 0x20;

    // ========================
    //  UClass hierarchy traversal (for IsA checks)
    // ========================
    constexpr uintptr_t UStruct_SuperStruct = 0x40;
}

// ========================
//  Dark and Darker character class enum
// ========================
enum class EDCCharacterClass : uint8_t {
    None      = 0,
    Wizard    = 1,
    Rogue     = 2,
    Ranger    = 3,
    Fighter   = 4,
    Cleric    = 5,
    Barbarian = 6,
    Bard      = 7,
    Warlock   = 8,
    Druid     = 9
};

inline const char* CharacterClassToString(EDCCharacterClass cls) {
    switch (cls) {
        case EDCCharacterClass::Wizard:    return "Wizard";
        case EDCCharacterClass::Rogue:     return "Rogue";
        case EDCCharacterClass::Ranger:    return "Ranger";
        case EDCCharacterClass::Fighter:   return "Fighter";
        case EDCCharacterClass::Cleric:    return "Cleric";
        case EDCCharacterClass::Barbarian: return "Barbarian";
        case EDCCharacterClass::Bard:      return "Bard";
        case EDCCharacterClass::Warlock:   return "Warlock";
        case EDCCharacterClass::Druid:     return "Druid";
        default:                           return "Unknown";
    }
}

// ========================
//  EDCItemRarity (from SDK: DungeonCrawler_structs.hpp)
// ========================
enum class EDCItemRarity : uint8_t {
    None     = 0,
    Poor     = 1,
    Common   = 2,
    Uncommon = 3,
    Rare     = 4,
    Epic     = 5,
    Legend   = 6,
    Unique   = 7,
    Artifact = 8
};

// ========================
//  EDCItemType (from SDK: DungeonCrawler_structs.hpp)
// ========================
enum class EDCItemType : uint8_t {
    None      = 0,
    Weapon    = 1,
    Armor     = 2,
    Utility   = 3,
    Accessory = 4,
    Misc      = 5,
    Misc_Gem  = 6
};

// ========================
//  FString layout for reading UE FString from memory
//  Data pointer + int32 Count + int32 Max
// ========================
struct FString {
    uintptr_t Data = 0;   // wchar_t* pointer
    int32_t Count = 0;     // String length (including null terminator)
    int32_t Max = 0;       // Allocated capacity
};
