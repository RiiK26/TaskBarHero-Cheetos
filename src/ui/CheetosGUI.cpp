#include "CheetosGUI.h"
#include "../utils/Security.h"
#include "../core/AOBScanner.h"  // IWYU pragma: keep

#ifndef EXPECTED_DLL_HASH
  #define EXPECTED_DLL_HASH ""
#endif
#include <thread>

#ifndef APP_VERSION
  #define APP_VERSION "Unknown"
#endif

void CheetosGUI::Run()
{
  // Hide Console Window
  HWND hConsole = GetConsoleWindow();
  if (hConsole)
    ShowWindow(hConsole, SW_HIDE);

  AddLog("Cheetos ready to attach to the game");

  // Setup window
  if (!glfwInit())
    return;

  // GL 3.0 + GLSL 130
  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  std::string windowTitle = std::string("TBH-Cheetos ") + APP_VERSION;
  GLFWwindow* window      = glfwCreateWindow(500, 600, windowTitle.c_str(), nullptr, nullptr);
  if (window == nullptr)
    return;

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void) io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

  // Setup Dear ImGui style (Dark Theme)
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

  // Main loop
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // We create one full-screen window for the gui
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(
      "Cheetos Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
    );

    DrawUI();

    ImGui::End();

    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(
      clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w
    );
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();

  // Detach from game upon exiting
  m_mem.Close();
}

void CheetosGUI::RenderTooltip(const char* text)
{
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

void CheetosGUI::DrawUI()
{


  // ---- Top Bar: Process Status ----
  bool isAttached = m_mem.IsAttached();
  if (isAttached) {
    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Cheetos Status: Attached");
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::Button("Detach")) {
      m_mem.Close();
      m_playerDataAddr = 0;
      AddLog("[System] Detached from game.");
    }
  }
  else {
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Cheetos Status: Not Attached");
    ImGui::SameLine(ImGui::GetWindowWidth() - 140);
    if (ImGui::Button("Attach to Game")) {
      if (m_mem.AttachByName(L"TaskBarHero.exe")) {
        m_playerDataAddr = 0;  // Clear stale pointers
        AddLog("[System] Attached successfully.");

        // Re-apply essential patches to new process
        AddLog(m_antiCheat.Apply());
        InjectSpeedhack();

        // Sync current UI state with the game
        SetSpeedhackEnable(m_speedhackEnabled);
        SetSpeedhack(m_speedValue);
      }
      else {
        AddLog("[System] Failed to attach. Is the game running?");
      }
    }
  }
  ImGui::Separator();

  // ---- God Mode Section ----
  const auto& targets = GodMode::GetAllTargets();

  // We need state for checkboxes and values.
  if (m_statStates.empty()) {
    m_statStates.resize(targets.size(), 1);  // Default all checked
    m_statValues.resize(targets.size(), 0.0f);
    for (size_t i = 0; i < targets.size(); i++) {
      m_godMode.SetStatEnabled(targets[i].type, true);
      m_statValues[i] = targets[i].cheatValue;
    }
  }

  if (ImGui::CollapsingHeader("God Mode Configuration")) {
    ImGui::Indent();
    for (size_t i = 0; i < targets.size(); i++) {
      bool checked = m_statStates[i];

      // Push ID to ensure ImGui elements have unique identifiers even if labels match
      ImGui::PushID((int) i);
      if (ImGui::Checkbox("##chk", &checked)) {
        m_statStates[i] = checked;
        m_godMode.SetStatEnabled(targets[i].type, checked);
      }
      ImGui::SameLine();

      // Display name fixed width
      ImGui::Text("%-20s", targets[i].displayName);
      // Custom Value Input
      ImGui::SameLine(180);
      ImGui::SetNextItemWidth(110);
      ImGui::InputFloat("##val", &m_statValues[i], 0.0f, 0.0f, "%.1f");

      // Tooltips for specific stats (rendered AFTER the input)
      if (targets[i].type == StatType::CriticalChance) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
          RenderTooltip("1.0 = 100%, 0.5 = 50%, 100.0 = 10000%");
      }
      else if (targets[i].type == StatType::MovementSpeed) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
          RenderTooltip("u can close this app if only enabling this without speedhack");
      }

      // Type-safe clamping
      if (m_statValues[i] < targets[i].minValue)
        m_statValues[i] = targets[i].minValue;
      if (m_statValues[i] > targets[i].maxValue)
        m_statValues[i] = targets[i].maxValue;

      ImGui::PopID();
    }
    ImGui::Unindent();
    ImGui::Spacing();
  }

  if (m_isScanning) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Apply God Mode", ImVec2(250, 20))) {
    m_isScanning = true;
    AddLog("[GodMode] Scanning for heroes...");

    // Capture custom values by value so the thread can access them safely
    std::vector<float> valuesToApply = m_statValues;

    std::thread([this, valuesToApply]() {
      // Save original stats BEFORE applying god mode
      if (!m_godModeActive) {
        SaveOriginalStats();
      }
      std::string result = m_godMode.Apply(m_heroFinder, valuesToApply);
      m_godModeActive    = true;
      PostLogFromThread(result);
      m_isScanning = false;
    }).detach();
  }
  if (m_isScanning) {
    ImGui::EndDisabled();
  }

  // Restore button
  if (m_godModeActive) {
    ImGui::SameLine();
    if (ImGui::Button("Restore Stats", ImVec2(120, 20))) {
      RestoreOriginalStats();
      AddLog("[GodMode] Original stats restored.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
      RenderTooltip("Restores original stat values");
  }

  ImGui::Spacing();

  ImGui::Text("Smart Rune Upgrader");
  ImGui::Separator();

  if (m_isRuneScanning) {
    ImGui::BeginDisabled();
    ImGui::Button("Scanning Memory... Please wait...", ImVec2(250, 20));
    ImGui::EndDisabled();
  }
  else {
    if (ImGui::Button("Scan / Refresh Runes", ImVec2(250, 20))) {
      m_isRuneScanning = true;
      AddLog("[Rune] Starting memory scan in background...");

      std::thread([this]() {
        // Clear stale cache and retry up to 2 times
        for (int attempt = 0; attempt < 2 && m_playerDataAddr == 0; attempt++) {
          if (attempt > 0) {
            m_playerFinder.ClearCache();
            PostLogFromThread("[Rune] Retrying scan (attempt " + std::to_string(attempt + 1) + ")...");
          }
          auto result = m_playerFinder.Find();
          if (result) {
            m_playerDataAddr = result->playerSaveDataAddr;
            m_runeMaxLevels  = result->runeMaxLevels;
          }
        }
        if (m_playerDataAddr != 0) {
          auto runeListPtr = m_mem.ReadPointer(m_playerDataAddr + PlayerSaveDataOffsets::RuneSaveData);
          if (runeListPtr && *runeListPtr != 0) {
            m_runeUnlocker.SetRuneListAddr(*runeListPtr);
            m_runeUnlocker.ScanRunes(m_playerFinder, m_runeMaxLevels);
            PostLogFromThread("[Rune] Scanned all runes successfully.");
          }
          else {
            PostLogFromThread("[Rune] Rune list not initialized yet (pointer is null).");
          }
        }
        else {
          PostLogFromThread("[Rune] PlayerSaveData not found! try again to scan or relog");
        }
        m_isRuneScanning = false;
      }).detach();
    }
  }

  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    RenderTooltip(
      "IMPORTANT: After upgrading runes, you MUST close both the game AND this app to trigger "
      "Steam Cloud Sync. If the app is still running, Steam will think the game is still open and "
      "won't sync!"
    );
  }

  const auto& runes = m_runeUnlocker.GetCachedRunes();
  if (!runes.empty() && !m_isRuneScanning) {
    ImGui::Spacing();

    bool hasLocked   = false;
    bool hasNotMaxed = false;
    for (const auto& r : runes) {
      if (r.level == 0)
        hasLocked = true;
      if (r.level > 0 && r.level < m_runeUnlocker.GetMaxLevelForRune(r.runeKey))
        hasNotMaxed = true;
    }

    // Smart Global Buttons
    if (hasLocked) {
      if (ImGui::Button("Unlock All Locked Runes (Level 0 -> 1)", ImVec2(300, 20))) {
        AddLog(m_runeUnlocker.UnlockAllLocked());
      }
    }

    if (hasNotMaxed) {
      if (ImGui::Button("Upgrade All To MAX Level Instantly", ImVec2(300, 20))) {
        AddLog(m_runeUnlocker.UpgradeAllToMax());
      }
    }

    if (hasNotMaxed && hasLocked) {
      if (ImGui::Button("Upgrade All Unlocked Runes by +1", ImVec2(300, 20))) {
        AddLog(m_runeUnlocker.UpgradeAllUnlocked(1));
      }
    }

    ImGui::Spacing();
    ImGui::Text("Runes (%zu found)", runes.size());

    // Table for Runes
    if (
      ImGui::BeginTable(
        "RuneTable", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
        ImVec2(0, 150)
      )
    ) {
      ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("Upgrade", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      for (const auto& ri : runes) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", ri.runeKey);

        ImGui::TableSetColumnIndex(1);
        if (ri.level == 0) {
          ImGui::TextColored(ImVec4(1, 0, 0, 1), "Locked");
        }
        else {
          ImGui::TextColored(ImVec4(0, 1, 0, 1), "Unlocked");
        }

        int  maxLvl  = m_runeUnlocker.GetMaxLevelForRune(ri.runeKey);
        bool isMaxed = (ri.level >= maxLvl);

        ImGui::TableSetColumnIndex(2);
        if (isMaxed) {
          ImGui::TextColored(ImVec4(1, 1, 0, 1), "%d / %d (MAX)", ri.level, maxLvl);
        }
        else {
          ImGui::Text("%d / %d", ri.level, maxLvl);
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::PushID(ri.runeKey);
        if (isMaxed) {
          ImGui::BeginDisabled();
          ImGui::Button("Maxed", ImVec2(75, 0));
          ImGui::EndDisabled();
        }
        else if (ri.level == 0) {
          if (ImGui::Button("Unlock", ImVec2(75, 0))) {
            AddLog(m_runeUnlocker.UpgradeRune(ri.runeKey, 1));
          }
        }
        else {
          if (ImGui::Button("+1 Upgrade", ImVec2(75, 0))) {
            AddLog(m_runeUnlocker.UpgradeRune(ri.runeKey, 1));
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }
  ImGui::Spacing();

  // ---- Speedhack Section ----
  ImGui::Text("Speedhack");
  ImGui::Separator();
  if (ImGui::Checkbox("Enable Speedhack", &m_speedhackEnabled)) {
    SetSpeedhackEnable(m_speedhackEnabled);
    if (m_speedhackEnabled) {
      SetSpeedhack(m_speedValue);
    }
    AddLog(m_speedhackEnabled ? "[Speedhack] Enabled." : "[Speedhack] Disabled.");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    RenderTooltip("Dont close this app while speedhack is enabled, or the game will crash");
  }

  // ImGui Slider for Speed
  if (m_speedhackEnabled) {
    ImGui::BeginDisabled();
  }
  if (ImGui::SliderFloat("Speed", &m_speedValue, 0.1f, 10.0f, "%.1fx")) {
    SetSpeedhack(m_speedValue);
  }
  if (m_speedhackEnabled) {
    ImGui::EndDisabled();
  }
  ImGui::Spacing();

  // ---- EXP Multiplier Section ----
  ImGui::Text("EXP Multiplier");
  ImGui::Separator();

  ImGui::SetNextItemWidth(150);
  ImGui::InputFloat("EXP Multiplier", &m_expMultiplierValue, 0.0f, 0.0f, "%.1f");
  if (m_expMultiplierValue < 1.0f)
    m_expMultiplierValue = 1.0f;
  if (m_expMultiplierValue > 99999999.0f)
    m_expMultiplierValue = 99999999.0f;

  if (ImGui::Button("Apply EXP Multiplier", ImVec2(250, 20))) {
    AddLog("[EXP Multiplier] Applying Multiplier... (Wait a moment)");
    float multiToApply = m_expMultiplierValue;
    std::thread([this, multiToApply]() {
      auto res = m_expMultiplier.ApplyMaxExp(multiToApply);
      PostLogFromThread(res.msg);
    }).detach();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    RenderTooltip(
      "After activated just kill monster like a normal, and you will see the exp increase rapidly "
      "or if u dont see it, wait until the hero reached max lvl (101). just wait for it."
    );
  }
  ImGui::Spacing();

  // ---- Log Section ----
  ImGui::Text("Log");
  ImGui::SameLine(ImGui::GetWindowWidth() - 80);
  if (ImGui::Button("Clear")) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logHistory.clear();
  }
  ImGui::Separator();

  // Process any incoming logs from background threads
  {
    std::lock_guard<std::mutex> lock(m_logMutex);
    while (!m_logQueue.empty()) {
      m_logHistory.push_back(m_logQueue.front());
      m_logQueue.pop();
      m_scrollToBottom = true;
    }
  }

  ImGui::BeginChild("LogRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& log : m_logHistory) {
    ImGui::TextUnformatted(log.c_str());
  }
  if (m_scrollToBottom) {
    ImGui::SetScrollHereY(1.0f);
    m_scrollToBottom = false;
  }
  ImGui::EndChild();
}

void CheetosGUI::AddLog(const std::string& text)
{
  if (text.empty())
    return;
  std::lock_guard<std::mutex> lock(m_logMutex);
  m_logQueue.push(text);
}

void CheetosGUI::PostLogFromThread(const std::string& msg) { AddLog(msg); }

void CheetosGUI::InjectSpeedhack()
{
  if (!m_mem.IsAttached())
    return;
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string path       = exePath;
  size_t      lastSlash  = path.find_last_of("\\/");
  std::string dllPathStr = path.substr(0, lastSlash) + "\\speedhack.dll";
  const char* dllPath    = dllPathStr.c_str();

  if (!Security::VerifyFileHashSha256(dllPathStr, EXPECTED_DLL_HASH)) {
    AddLog("[Security] speedhack.dll hash mismatch! Injection aborted.");
    return;
  }

  LPVOID pRemoteMem = VirtualAllocEx(m_mem.Handle(), NULL, strlen(dllPath) + 1, MEM_COMMIT, PAGE_READWRITE);
  if (!pRemoteMem)
    return;

  SIZE_T bytesWritten;
  WriteProcessMemory(m_mem.Handle(), pRemoteMem, dllPath, strlen(dllPath) + 1, &bytesWritten);

  HMODULE hKernel32    = GetModuleHandle("kernel32.dll");
  LPVOID  pLoadLibrary = (LPVOID) GetProcAddress(hKernel32, "LoadLibraryA");

  HANDLE hThread =
    CreateRemoteThread(m_mem.Handle(), NULL, 0, (LPTHREAD_START_ROUTINE) pLoadLibrary, pRemoteMem, 0, NULL);
  if (hThread) {
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    AddLog("[Speedhack] DLL Injected successfully.");
  }
  else {
    AddLog("[Speedhack] Failed to inject DLL.");
  }
  VirtualFreeEx(m_mem.Handle(), pRemoteMem, 0, MEM_RELEASE);
}

void CheetosGUI::SetSpeedhack(float speed)
{
  if (!m_mem.IsAttached())
    return;
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string path       = exePath;
  size_t      lastSlash  = path.find_last_of("\\/");
  std::string dllPathStr = path.substr(0, lastSlash) + "\\speedhack.dll";

  HMODULE hMods[1024];
  DWORD   cbNeeded;
  if (EnumProcessModulesEx(m_mem.Handle(), hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
    for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
      char szModName[MAX_PATH];
      if (GetModuleFileNameExA(m_mem.Handle(), hMods[i], szModName, sizeof(szModName))) {
        if (strstr(szModName, "speedhack.dll")) {
          HMODULE hLocalDll = LoadLibraryEx(dllPathStr.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
          if (hLocalDll) {
            void* pLocalVar = (void*) GetProcAddress(hLocalDll, "g_SpeedMultiplier");
            if (pLocalVar) {
              uintptr_t offset    = (uintptr_t) pLocalVar - (uintptr_t) hLocalDll;
              uintptr_t remoteVar = (uintptr_t) hMods[i] + offset;
              m_mem.WriteFloat(remoteVar, speed);
            }
            FreeLibrary(hLocalDll);
          }
        }
      }
    }
  }
}

void CheetosGUI::SetSpeedhackEnable(bool enable)
{
  if (!m_mem.IsAttached())
    return;
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string path       = exePath;
  size_t      lastSlash  = path.find_last_of("\\/");
  std::string dllPathStr = path.substr(0, lastSlash) + "\\speedhack.dll";

  HMODULE hMods[1024];
  DWORD   cbNeeded;
  if (EnumProcessModulesEx(m_mem.Handle(), hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL)) {
    for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
      char szModName[MAX_PATH];
      if (GetModuleFileNameExA(m_mem.Handle(), hMods[i], szModName, sizeof(szModName))) {
        if (strstr(szModName, "speedhack.dll")) {
          HMODULE hLocalDll = LoadLibraryEx(dllPathStr.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
          if (hLocalDll) {
            void* pLocalVar = (void*) GetProcAddress(hLocalDll, "g_SpeedhackEnabled");
            if (pLocalVar) {
              uintptr_t offset    = (uintptr_t) pLocalVar - (uintptr_t) hLocalDll;
              uintptr_t remoteVar = (uintptr_t) hMods[i] + offset;
              m_mem.WriteBool(remoteVar, enable);
            }
            FreeLibrary(hLocalDll);
          }
        }
      }
    }
  }
}

void CheetosGUI::SaveOriginalStats()
{
  m_statBackups.clear();

  auto results = m_heroFinder.FindAll(true);
  if (results.empty())
    return;

  const auto& targets = GodMode::GetAllTargets();
  for (const auto& hr : results) {
    for (const auto& target : targets) {
      if (!m_godMode.IsStatEnabled(target.type))
        continue;

      auto it = hr.stats.find(target.type);
      if (it != hr.stats.end()) {
        StatBackup backup;
        backup.address       = it->second.address;
        backup.originalValue = it->second.value;
        m_statBackups.push_back(backup);
      }
    }
  }

  if (!m_statBackups.empty()) {
    PostLogFromThread("[GodMode] Saved " + std::to_string(m_statBackups.size()) + " original stat values for restore.");
  }
}

void CheetosGUI::RestoreOriginalStats()
{
  if (m_statBackups.empty() || !m_mem.IsAttached()) {
    m_godModeActive = false;
    return;
  }

  int restored = 0;
  for (const auto& backup : m_statBackups) {
    if (m_mem.WriteFloat(backup.address, backup.originalValue)) {
      restored++;
    }
  }

  PostLogFromThread(
    "[GodMode] Restored " + std::to_string(restored) + "/" + std::to_string(m_statBackups.size()) + " stats."
  );
  m_statBackups.clear();
  m_godModeActive = false;
}
