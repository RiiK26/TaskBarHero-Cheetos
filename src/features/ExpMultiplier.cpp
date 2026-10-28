#include "ExpMultiplier.h"
#include "../scanner/HeroFinder.h"

ExpMultiplier::ExpMultiplier(ProcessMemory& mem) :
    m_mem(mem)
{
}
ExpMultiplier::~ExpMultiplier() { }

ExpMultiplier::Result ExpMultiplier::ApplyMaxExp(float multiplier)
{
  HeroFinder heroFinder(m_mem);
  return ApplyMaxExp(heroFinder, multiplier);
}

ExpMultiplier::Result ExpMultiplier::ApplyMaxExp(HeroFinder& heroFinder, float multiplier)
{
  if (!m_mem.IsAttached())
    return {false, "[Exp Multiplier] Not attached to game."};

  auto results = heroFinder.FindAll(true);

  if (results.empty())
    return {false, "[EXP Multiplier] Could not find Active Hero in memory. Enter a stage first."};

  int updatedHeroes = 0;

  for (const auto& hr : results) {
    // StatType::IncreaseExpAmount = 47
    auto it = hr.stats.find(StatType::IncreaseExpAmount);
    if (it != hr.stats.end()) {
      if (m_mem.WriteFloat(it->second.address, multiplier)) {
        updatedHeroes++;
      }
    }
  }

  char buf[256];
  if (updatedHeroes > 0) {
    snprintf(buf, sizeof(buf), "[EXP Multiplier] EXP Multiplier applied to %d active heroes!", updatedHeroes);
    return {true, buf};
  }
  else {
    return {false, "[EXP Multiplier] Found Hero, but EXP Stat not found in dictionary."};
  }
}
