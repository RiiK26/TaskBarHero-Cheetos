#pragma once
#include <cstdint>

// =========================================================================
// Runtime Hero Stats Chain
// =========================================================================

struct HeroInfoDataOffsets
{
  static constexpr int32_t HeroKey            = 0x30;  // @[HeroInfoData.HeroKey] [UNCHANGED]
  static constexpr int32_t HeroNameKey        = 0x38;  // @[HeroInfoData.HeroNameKey] [UNCHANGED]
  static constexpr int32_t DescriptionKey     = 0x40;  // @[HeroInfoData.DescriptionKey] [UNCHANGED]
  static constexpr int32_t ClassType          = 0x48;  // @[HeroInfoData.ClassType] [UNCHANGED]
  static constexpr int32_t MainWeaponGearType = 0x4C;  // @[HeroInfoData.MainWeaponGearType] [UNCHANGED]
  static constexpr int32_t SubWeaponGearType  = 0x50;  // @[HeroInfoData.SubWeaponGearType] [UNCHANGED]
  static constexpr int32_t SkillKey           = 0x54;  // @[HeroInfoData.SkillKey] [UNCHANGED]
  static constexpr int32_t AttackDamage       = 0x58;  // @[HeroInfoData.AttackDamage] [UNCHANGED]
  static constexpr int32_t AttackSpeed        = 0x5C;  // @[HeroInfoData.AttackSpeed] [UNCHANGED]
  static constexpr int32_t CastSpeed          = 0x60;  // @[HeroInfoData.CastSpeed] [UNCHANGED]
  static constexpr int32_t CriticalChance     = 0x64;  // @[HeroInfoData.CriticalChance] [UNCHANGED]
  static constexpr int32_t CriticalDamage     = 0x68;  // @[HeroInfoData.CriticalDamage] [UNCHANGED]
  static constexpr int32_t CooldownReduction  = 0x6C;  // @[HeroInfoData.CooldownReduction] [UNCHANGED]
  static constexpr int32_t MaxHp              = 0x70;  // @[HeroInfoData.MaxHp] [UNCHANGED]
  static constexpr int32_t Armor              = 0x74;  // @[HeroInfoData.Armor] [UNCHANGED]
  static constexpr int32_t MovementSpeed      = 0x78;  // @[HeroInfoData.MovementSpeed] [UNCHANGED]
  static constexpr int32_t UnlockCost         = 0x7C;  // @[HeroInfoData.UnlockCost] [UNCHANGED]
  static constexpr int32_t IsMeleeHero        = 0x80;  // @[HeroInfoData.IsMeleeHero] [UNCHANGED]
  static constexpr int32_t IsAvailable        = 0x81;  // @[HeroInfoData.IsAvailable] [UNCHANGED]
  static constexpr int32_t IsFirstAvailable   = 0x82;  // @[HeroInfoData.IsFirstAvailable] [UNCHANGED]
  static constexpr int32_t SelectSoundKey     = 0x84;  // @[HeroInfoData.SelectSoundKey] [UNCHANGED]
  static constexpr int32_t DeadSoundKey       = 0x88;  // @[HeroInfoData.DeadSoundKey] [UNCHANGED]
  static constexpr int32_t IconPath           = 0x90;  // @[HeroInfoData.IconPath] [UNCHANGED]
  static constexpr int32_t DeadIconPath       = 0x98;  // @[HeroInfoData.DeadIconPath] [UNCHANGED]
  static constexpr int32_t PrefabPath         = 0xA0;  // @[HeroInfoData.PrefabPath] [UNCHANGED]
  static constexpr int32_t AnimatorPath       = 0xA8;  // @[HeroInfoData.AnimatorPath] [UNCHANGED]
  static constexpr int32_t DLCAppId           = 0xB0;  // @[HeroInfoData.DLCAppId] [UNCHANGED]
  static constexpr int32_t DLCBitIndex        = 0xB4;  // @[HeroInfoData.DLCBitIndex] [UNCHANGED]
  static constexpr int32_t HasDLCDrop         = 0xB8;  // @[HeroInfoData.HasDLCDrop] [UNCHANGED]
};

struct Il2CppOffsets
{
  // HeroInfoData (static/base data) — confirmed
  static constexpr int32_t HeroInfoData_HeroKey     = 0x30;
  static constexpr int32_t HeroInfoData_HeroNameKey = 0x38;

  // vo base class (inherited into vh) — confirmed
  static constexpr int32_t Vo_StatContainer = 0x10;  // @[wn.StatContainerBackingField] [UNCHANGED]

  // vh derived class — confirmed
  static constexpr int32_t Vh_HeroInfoDataRef = 0x30;  // @[wg.HeroInfoDataRef] [UNCHANGED]
  static constexpr int32_t Vh_HeroBackRef     = 0x88;  // @[wg.HeroBackRef] [UNCHANGED]

  // ze stat container - confirmed
  static constexpr int32_t Ze_StatsDictA = 0x18;  // @[StatContainer.StatsDictA] [UNCHANGED]
  static constexpr int32_t Ze_StatsDictB = 0x20;  // @[StatContainer.StatsDictB] [UNCHANGED]
};

// =========================================================================
// Save Data Offsets
// =========================================================================

// Save Manager
// This is a singleton MonoBehaviour that holds the save data.
struct SaveManagerOffsets
{
  static constexpr int32_t AccountSaveData = 0x20;  // @[SaveManager.AccountSaveData] [UNCHANGED]
  static constexpr int32_t PlayerSaveData  = 0x28;  // @[SaveManager.PlayerSaveData] [UNCHANGED]
};

// PlayerSaveData (TypeDefIndex: 844)
struct PlayerSaveDataOffsets
{
  static constexpr int32_t CommonSaveData     = 0x10;  // @[PlayerSaveData.commonSaveData] [UNCHANGED]
  static constexpr int32_t SettingSaveData    = 0x18;  // @[PlayerSaveData.settingSaveData] [UNCHANGED]
  static constexpr int32_t BoxData            = 0x20;  // @[PlayerSaveData.BoxBucketUseBoxList] [UNCHANGED]
  static constexpr int32_t CurrencySaveDatas  = 0x60;  // @[PlayerSaveData.currenySaveDatas] [UNCHANGED]
  static constexpr int32_t HeroSaveDatas      = 0x68;  // @[PlayerSaveData.heroSaveDatas] [UNCHANGED]
  static constexpr int32_t AttributeSaveDatas = 0x78;  // @[PlayerSaveData.attributeSaveDatas] [UNCHANGED]
  static constexpr int32_t PetSaveData        = 0x80;  // @[PlayerSaveData.PetSaveData] [UNCHANGED]
  static constexpr int32_t RuneSaveData       = 0x88;  // @[PlayerSaveData.RuneSaveData] [UNCHANGED]
  static constexpr int32_t InventorySaveDatas = 0x90;  // @[PlayerSaveData.inventorySaveDatas] [UNCHANGED]
  static constexpr int32_t CubeSaveLevelData  = 0xB0;  // @[PlayerSaveData.cubeSaveLevelData] [UNCHANGED]
};

// HeroSaveData (TypeDefIndex: 1258)
struct HeroSaveDataOffsets
{
  static constexpr int32_t HeroKey   = 0x10;  // @[HeroSaveData.heroKey] [UNCHANGED]
  static constexpr int32_t HeroLevel = 0x14;  // @[HeroSaveData.HeroLevel] [UNCHANGED]
  static constexpr int32_t IsUnLock  = 0x18;  // @[HeroSaveData.IsUnLock] [UNCHANGED]
  static constexpr int32_t HeroExp   = 0x20;  // @[HeroSaveData.HeroExp] [UNCHANGED]
};

// CurrencySaveData (TypeDefIndex: 1257)
struct CurrencySaveDataOffsets
{
  static constexpr int32_t Key      = 0x10;  // @[CurrencySaveData.Key] [UNCHANGED]
  static constexpr int32_t Quantity = 0x18;  // @[CurrencySaveData.Quantity] [UNCHANGED]
};

// RuneSaveData (TypeDefIndex: 1266)
struct RuneSaveDataOffsets
{
  static constexpr int32_t RuneKey = 0x10;  // @[RuneSaveData.RuneKey] [UNCHANGED]
  static constexpr int32_t Level   = 0x14;  // @[RuneSaveData.Level] [UNCHANGED]
};

// CubeLevelSaveData
struct CubeLevelSaveDataOffsets
{
  static constexpr int32_t Level = 0x10;  // @[CubeLevelSaveData.Level] [UNCHANGED]
  static constexpr int32_t Exp   = 0x14;  // @[CubeLevelSaveData.Exp] [UNCHANGED]
};

// CubeRecipeSaveData (TypeDefIndex: 1337)
struct CubeRecipeSaveDataOffsets
{
  static constexpr int32_t CubeRecipeTypeInt  = 0x10;  // @[CubeRecipeSaveData.CubeRecipeTypeInt] [UNCHANGED]
  static constexpr int32_t CubeKey            = 0x14;  // @[CubeRecipeSaveData.CubeKey] [UNCHANGED]
  static constexpr int32_t MaxUnlockRecipeKey = 0x18;  // @[CubeRecipeSaveData.MaxUnlockRecipeKey] [UNCHANGED]
};

// =========================================================================
// StageManager (TypeDefIndex: 835, extends oa<StageManager> singleton)
// =========================================================================
struct StageManagerOffsets
{
  static constexpr int32_t HeroList = 0x30;   // @[StageManager.HeroList] [UNCHANGED]
  static constexpr int32_t OnGetBox = 0x110;  // @[StageManager.OnGetBox] [UNCHANGED]
};

// EBoxType values (TypeDefIndex: 1006)
struct EBoxType
{
  static constexpr int32_t NORMAL  = 0;
  static constexpr int32_t BOSS    = 1;
  static constexpr int32_t ACTBOSS = 2;
};

// EContentType values (TypeDefIndex: 1671)
struct EContentType
{
  static constexpr int32_t NONE   = 0;
  static constexpr int32_t PLAGUE = 1;
};

// ECubeSynthesisResult values (TypeDefIndex: 995)
struct ECubeSynthesisResult
{
  static constexpr int32_t Fail         = 0;
  static constexpr int32_t Success      = 1;
  static constexpr int32_t GreatSuccess = 2;
};

// ERecipeType values (TypeDefIndex: 1625)
struct ERecipeType
{
  static constexpr int32_t ALCHEMY     = 0;
  static constexpr int32_t SYNTHESIS   = 1;
  static constexpr int32_t CRAFTING    = 2;
  static constexpr int32_t DECORATION  = 3;
  static constexpr int32_t ENGRAVING   = 4;
  static constexpr int32_t INSCRIPTION = 5;
  static constexpr int32_t OFFERING    = 6;
  static constexpr int32_t EXTRACTION  = 7;
  static constexpr int32_t NONE        = 8;
  static constexpr int32_t CORROSION   = 9;
};

// we.ux.BoxData struct (TypeDefIndex: 927)
struct BoxDataOffsets
{
  static constexpr int32_t UniqueId = 0x0;  // ulong
  static constexpr int32_t BoxType  = 0x8;  // EBoxType (int)
};

// SerializedBoxData (TypeDefIndex: 864)
struct SerializedBoxDataOffsets
{
  static constexpr int32_t BoxTypes    = 0x10;  // List<EBoxType>
  static constexpr int32_t BoxUniqueId = 0x18;  // List<ulong>
  static constexpr int32_t BoxQuantity = 0x20;  // List<int>
};

// =========================================================================
// IL2CPP Generic Container Offsets
// =========================================================================

// Standard IL2CPP List<T> layout
struct Il2CppListOffsets
{
  static constexpr int32_t Items   = 0x10;  // T[] _items (backing array pointer)
  static constexpr int32_t Size    = 0x18;  // int _size
  static constexpr int32_t Version = 0x1C;  // int _version
};

// Standard IL2CPP Array header
struct Il2CppArrayOffsets
{
  static constexpr int32_t Length = 0x18;  // int length
  static constexpr int32_t Data   = 0x20;  // first element starts here
  // Each element is sizeof(T) for value types, or sizeof(pointer) for ref types
};

// Standard IL2CPP String header
struct Il2CppStringOffsets
{
  static constexpr int32_t Length = 0x10;  // int length
  static constexpr int32_t Chars  = 0x14;  // first char starts here
};

// Standard IL2CPP Dictionary<TKey, TValue> layout
struct Il2CppDictOffsets
{
  static constexpr int32_t Buckets        = 0x10;  // int[] _buckets
  static constexpr int32_t Entries        = 0x18;  // Entry[] _entries
  static constexpr int32_t Count          = 0x20;  // int _count
  static constexpr int32_t ArrayLength    = 0x18;  // array header length
  static constexpr int32_t ArrayData      = 0x20;  // array data start
  static constexpr int32_t FreeCount      = 0x28;  // int _freeCount
  static constexpr int32_t EntrySize      = 16;    // { int hashCode; int next; TKey; TValue }
  static constexpr int32_t EntryKeyOffset = 8;     // offset to key within entry
  static constexpr int32_t EntryValOffset = 12;    // offset to value within entry
};

// Global instance accessor
inline const Il2CppOffsets& CurrentOffsets()
{
  static Il2CppOffsets offsets;
  return offsets;
}
