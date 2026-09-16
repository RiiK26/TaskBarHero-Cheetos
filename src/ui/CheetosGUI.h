#pragma once
#include "../core/ProcessMemory.h"
#include "../features/GodMode.h"
#include "features/AntiCheatBypass.h"
#include "features/ExpMultiplier.h"
#include "features/RuneUnlocker.h"
#include "scanner/HeroFinder.h"
#include "scanner/PlayerDataFinder.h"

// ImGui and GLFW
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <windows.h>
#include <psapi.h>

class CheetosGUI
{
public:
  CheetosGUI(ProcessMemory& mem) :
      m_mem(mem),
      m_godMode(mem),
      m_runeUnlocker(mem),
      m_antiCheat(mem),
      m_expMultiplier(mem),
      m_heroFinder(mem),
      m_playerFinder(mem)
  {
  }

  void Run();

private:
  void DrawUI();
  void RenderTooltip(const char* text);

  void AddLog(const std::string& text);
  void PostLogFromThread(const std::string& msg);

  void InjectSpeedhack();
  void SetSpeedhack(float speed);
  void SetSpeedhackEnable(bool enable);

  void SaveOriginalStats();
  void RestoreOriginalStats();

  ProcessMemory&   m_mem;
  GodMode          m_godMode;
  RuneUnlocker     m_runeUnlocker;
  AntiCheatBypass  m_antiCheat;
  ExpMultiplier    m_expMultiplier;
  HeroFinder       m_heroFinder;
  PlayerDataFinder m_playerFinder;

  uintptr_t          m_playerDataAddr = 0;
  std::map<int, int> m_runeMaxLevels;

  // Runes State
  std::atomic<bool> m_isRuneScanning{false};

  // UI State
  float             m_expMultiplierValue = 99999.0f;
  bool              m_speedhackEnabled   = false;
  float             m_speedValue         = 1.0f;
  std::atomic<bool> m_isScanning{false};

  std::vector<uint8_t> m_statStates;
  std::vector<float>   m_statValues;

  // GodMode stat backup (for restore on detach)
  struct StatBackup
  {
    uintptr_t address;
    float     originalValue;
  };
  std::vector<StatBackup> m_statBackups;
  bool                    m_godModeActive = false;

  // Log State
  std::vector<std::string> m_logHistory;
  bool                     m_scrollToBottom = false;
  std::mutex               m_logMutex;
  std::queue<std::string>  m_logQueue;
};
