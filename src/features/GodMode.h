#pragma once
#include "../core/Il2CppStatDictionary.h"
#include "../core/ProcessMemory.h"
#include "../core/StatType.h"
#include "../scanner/HeroFinder.h"

#include <string>
#include <vector>
#include <set>

class GodMode
{
public:
  struct CheatTarget
  {
    StatType    type;
    float       cheatValue;
    float       minValue;
    float       maxValue;
    const char* displayName;
  };

  explicit GodMode(ProcessMemory& mem) :
      m_mem(mem)
  {
    // Enable all stats by default
    for (const auto& t : GetAllTargets()) {
      m_enabledStats.insert(t.type);
    }
  }

  // Enable/disable a specific stat for the next Apply
  void SetStatEnabled(StatType type, bool enabled)
  {
    if (enabled)
      m_enabledStats.insert(type);
    else
      m_enabledStats.erase(type);
  }

  bool IsStatEnabled(StatType type) const { return m_enabledStats.count(type) > 0; }

  // One-shot: scan for heroes and write selected stats.
  std::string Apply(HeroFinder& finder, const std::vector<float>& customValues)
  {
    if (m_enabledStats.empty())
      return "[GodMode] No stats selected. Check at least one stat.";

    auto results = finder.FindAll(true);

    if (results.empty())
      return "[GodMode] No heroes found in memory. Make sure you are in-game.";

    int         writeCount = 0;
    const auto& targets    = GetAllTargets();
    for (const auto& hr : results) {
      for (size_t i = 0; i < targets.size(); i++) {
        const auto& target = targets[i];
        if (m_enabledStats.count(target.type) == 0)
          continue;

        float valToWrite = (i < customValues.size()) ? customValues[i] : target.cheatValue;

        auto it          = hr.stats.find(target.type);
        if (it != hr.stats.end()) {
          if (m_mem.WriteFloat(it->second.address, valToWrite)) {
            writeCount++;
          }
        }
      }
    }

    return "[GodMode] Injected " + std::to_string(writeCount) + " stats across " + std::to_string(results.size())
         + " heroes.";
  }

  static const std::vector<CheatTarget>& GetAllTargets()
  {
    static const std::vector<CheatTarget> targets = {
      {StatType::MaxHp,          999999999.0f, 1.0f, 2000000000.0f, "Max HP"         },
      {StatType::AttackSpeed,    999999999.0f, 0.1f, 2000000000.0f, "Attack Speed"   },
      {StatType::CriticalChance, 1.0f,         0.0f, 100.0f,        "Crit Chance (%)"}, // 100%
      {StatType::CriticalDamage, 999999999.0f, 0.0f, 2000000000.0f, "Crit Damage"    },
      {StatType::Armor,          999999999.0f, 0.0f, 2000000000.0f, "Armor"          },
      {StatType::MovementSpeed,  999999999.0f, 0.0f, 1000000000.0f, "Movement Speed" },
      {StatType::CastSpeed,      999999999.0f, 0.1f, 2000000000.0f, "Cast Speed"     },
    };
    return targets;
  }

private:
  ProcessMemory&     m_mem;
  std::set<StatType> m_enabledStats;
};
