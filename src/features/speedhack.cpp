#include <windows.h>
#include <cstdint>
#include <atomic>
#include <emmintrin.h>
#include "MinHook.h"

extern "C" __declspec(dllexport) float g_SpeedMultiplier  = 1.0f;
extern "C" __declspec(dllexport) bool  g_SpeedhackEnabled = false;

struct TimeAnchors
{
  LONGLONG  qpcReal{0};
  LONGLONG  qpcFake{0};
  DWORD     tickReal{0};
  DWORD     tickFake{0};
  ULONGLONG tick64Real{0};
  ULONGLONG tick64Fake{0};
  DWORD     tgtReal{0};
  DWORD     tgtFake{0};
  ULONGLONG ftReal{0};
  ULONGLONG ftFake{0};
  float     speed{1.0f};
};

TimeAnchors      g_Anchors;
std::atomic_flag g_SpinLock    = ATOMIC_FLAG_INIT;
bool             g_Initialized = false;

inline void Lock()
{
  while (g_SpinLock.test_and_set(std::memory_order_acquire)) {
    _mm_pause();  // Extremely fast hardware thread pause
  }
}
inline void Unlock() { g_SpinLock.clear(std::memory_order_release); }

typedef BOOL(WINAPI* tQueryPerformanceCounter)(LARGE_INTEGER* lpPerformanceCount);
typedef DWORD(WINAPI* tGetTickCount)();
typedef ULONGLONG(WINAPI* tGetTickCount64)();
typedef DWORD(WINAPI* tTimeGetTime)();
typedef VOID(WINAPI* tGetSystemTimeAsFileTime)(LPFILETIME lpSystemTimeAsFileTime);
typedef VOID(WINAPI* tGetSystemTimePreciseAsFileTime)(LPFILETIME lpSystemTimeAsFileTime);

tQueryPerformanceCounter        oQueryPerformanceCounter        = nullptr;
tGetTickCount                   oGetTickCount                   = nullptr;
tGetTickCount64                 oGetTickCount64                 = nullptr;
tTimeGetTime                    oTimeGetTime                    = nullptr;
tGetSystemTimeAsFileTime        oGetSystemTimeAsFileTime        = nullptr;
tGetSystemTimePreciseAsFileTime oGetSystemTimePreciseAsFileTime = nullptr;

void InitializeAnchors()
{
  if (g_Initialized)
    return;

  LARGE_INTEGER qpc;
  if (oQueryPerformanceCounter && oQueryPerformanceCounter(&qpc)) {
    g_Anchors.qpcReal = qpc.QuadPart;
    g_Anchors.qpcFake = qpc.QuadPart;
  }
  if (oGetTickCount) {
    g_Anchors.tickReal = oGetTickCount();
    g_Anchors.tickFake = g_Anchors.tickReal;
  }
  if (oGetTickCount64) {
    g_Anchors.tick64Real = oGetTickCount64();
    g_Anchors.tick64Fake = g_Anchors.tick64Real;
  }
  if (oTimeGetTime) {
    g_Anchors.tgtReal = oTimeGetTime();
    g_Anchors.tgtFake = g_Anchors.tgtReal;
  }
  if (oGetSystemTimeAsFileTime) {
    FILETIME ft;
    oGetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart      = ft.dwLowDateTime;
    uli.HighPart     = ft.dwHighDateTime;
    g_Anchors.ftReal = uli.QuadPart;
    g_Anchors.ftFake = g_Anchors.ftReal;
  }
  g_Anchors.speed = g_SpeedhackEnabled ? g_SpeedMultiplier : 1.0f;
  g_Initialized   = true;
}

void CheckSpeedChange()
{
  float wantedSpeed = g_SpeedhackEnabled ? g_SpeedMultiplier : 1.0f;

  if (wantedSpeed != g_Anchors.speed) {
    LARGE_INTEGER qpc;
    if (oQueryPerformanceCounter && oQueryPerformanceCounter(&qpc)) {
      LONGLONG delta = qpc.QuadPart - g_Anchors.qpcReal;
      if (delta < 0)
        delta = 0;
      g_Anchors.qpcFake += (LONGLONG) ((double) delta * g_Anchors.speed);
      g_Anchors.qpcReal = qpc.QuadPart;
    }

    if (oGetTickCount) {
      DWORD tick  = oGetTickCount();
      DWORD delta = tick - g_Anchors.tickReal;
      if ((int32_t) delta < 0)
        delta = 0;
      g_Anchors.tickFake += (DWORD) ((double) delta * g_Anchors.speed);
      g_Anchors.tickReal = tick;
    }

    if (oGetTickCount64) {
      ULONGLONG tick  = oGetTickCount64();
      LONGLONG  delta = (LONGLONG) (tick - g_Anchors.tick64Real);
      if (delta < 0)
        delta = 0;
      g_Anchors.tick64Fake += (ULONGLONG) ((double) delta * g_Anchors.speed);
      g_Anchors.tick64Real = tick;
    }

    if (oTimeGetTime) {
      DWORD tick  = oTimeGetTime();
      DWORD delta = tick - g_Anchors.tgtReal;
      if ((int32_t) delta < 0)
        delta = 0;
      g_Anchors.tgtFake += (DWORD) ((double) delta * g_Anchors.speed);
      g_Anchors.tgtReal = tick;
    }

    if (oGetSystemTimeAsFileTime) {
      FILETIME ft;
      oGetSystemTimeAsFileTime(&ft);
      ULARGE_INTEGER uli;
      uli.LowPart    = ft.dwLowDateTime;
      uli.HighPart   = ft.dwHighDateTime;
      LONGLONG delta = (LONGLONG) (uli.QuadPart - g_Anchors.ftReal);
      if (delta < 0)
        delta = 0;
      g_Anchors.ftFake += (ULONGLONG) ((double) delta * g_Anchors.speed);
      g_Anchors.ftReal = uli.QuadPart;
    }

    g_Anchors.speed = wantedSpeed;
  }
}

BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
  BOOL result = oQueryPerformanceCounter(lpPerformanceCount);
  if (!result)
    return result;

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  LONGLONG delta = lpPerformanceCount->QuadPart - g_Anchors.qpcReal;
  if (delta < 0) {
    lpPerformanceCount->QuadPart = g_Anchors.qpcFake;  // Prevent drift jumps
  }
  else {
    lpPerformanceCount->QuadPart = g_Anchors.qpcFake + (LONGLONG) ((double) delta * g_Anchors.speed);
  }
  Unlock();

  return result;
}

DWORD WINAPI hkGetTickCount()
{
  DWORD real = oGetTickCount();

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  DWORD delta = real - g_Anchors.tickReal;
  DWORD result;
  if ((int32_t) delta < 0) {
    result = g_Anchors.tickFake;
  }
  else {
    result = g_Anchors.tickFake + (DWORD) ((double) delta * g_Anchors.speed);
  }
  Unlock();

  return result;
}

DWORD WINAPI hkTimeGetTime()
{
  DWORD real = oTimeGetTime();

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  DWORD delta = real - g_Anchors.tgtReal;
  DWORD result;
  if ((int32_t) delta < 0) {
    result = g_Anchors.tgtFake;
  }
  else {
    result = g_Anchors.tgtFake + (DWORD) ((double) delta * g_Anchors.speed);
  }
  Unlock();

  return result;
}

ULONGLONG WINAPI hkGetTickCount64()
{
  ULONGLONG real = oGetTickCount64();

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  LONGLONG  delta = (LONGLONG) (real - g_Anchors.tick64Real);
  ULONGLONG result;
  if (delta < 0) {
    result = g_Anchors.tick64Fake;
  }
  else {
    result = g_Anchors.tick64Fake + (ULONGLONG) ((double) delta * g_Anchors.speed);
  }
  Unlock();

  return result;
}

VOID WINAPI hkGetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
  if (!oGetSystemTimeAsFileTime)
    return;
  oGetSystemTimeAsFileTime(lpSystemTimeAsFileTime);

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  ULARGE_INTEGER uli;
  uli.LowPart    = lpSystemTimeAsFileTime->dwLowDateTime;
  uli.HighPart   = lpSystemTimeAsFileTime->dwHighDateTime;

  LONGLONG delta = (LONGLONG) (uli.QuadPart - g_Anchors.ftReal);
  if (delta < 0) {
    uli.QuadPart = g_Anchors.ftFake;
  }
  else {
    uli.QuadPart = g_Anchors.ftFake + (ULONGLONG) ((double) delta * g_Anchors.speed);
  }

  lpSystemTimeAsFileTime->dwLowDateTime  = uli.LowPart;
  lpSystemTimeAsFileTime->dwHighDateTime = uli.HighPart;
  Unlock();
}

VOID WINAPI hkGetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
  if (!oGetSystemTimePreciseAsFileTime)
    return;
  oGetSystemTimePreciseAsFileTime(lpSystemTimeAsFileTime);

  Lock();
  if (!g_Initialized)
    InitializeAnchors();
  CheckSpeedChange();

  ULARGE_INTEGER uli;
  uli.LowPart    = lpSystemTimeAsFileTime->dwLowDateTime;
  uli.HighPart   = lpSystemTimeAsFileTime->dwHighDateTime;

  LONGLONG delta = (LONGLONG) (uli.QuadPart - g_Anchors.ftReal);
  if (delta < 0) {
    uli.QuadPart = g_Anchors.ftFake;
  }
  else {
    uli.QuadPart = g_Anchors.ftFake + (ULONGLONG) ((double) delta * g_Anchors.speed);
  }

  lpSystemTimeAsFileTime->dwLowDateTime  = uli.LowPart;
  lpSystemTimeAsFileTime->dwHighDateTime = uli.HighPart;
  Unlock();
}

DWORD WINAPI MainThread(LPVOID lpParam)
{
  if (MH_Initialize() != MH_OK)
    return 0;

  MH_CreateHookApi(
    L"kernel32.dll", "QueryPerformanceCounter", (LPVOID) &hkQueryPerformanceCounter, (LPVOID*) &oQueryPerformanceCounter
  );
  MH_CreateHookApi(L"kernel32.dll", "GetTickCount", (LPVOID) &hkGetTickCount, (LPVOID*) &oGetTickCount);
  MH_CreateHookApi(L"kernel32.dll", "GetTickCount64", (LPVOID) &hkGetTickCount64, (LPVOID*) &oGetTickCount64);
  MH_CreateHookApi(
    L"kernel32.dll", "GetSystemTimeAsFileTime", (LPVOID) &hkGetSystemTimeAsFileTime, (LPVOID*) &oGetSystemTimeAsFileTime
  );
  MH_CreateHookApi(L"winmm.dll", "timeGetTime", (LPVOID) &hkTimeGetTime, (LPVOID*) &oTimeGetTime);

  HMODULE hKernel32 = GetModuleHandle("kernel32.dll");
  if (hKernel32) {
    FARPROC pPrecise = GetProcAddress(hKernel32, "GetSystemTimePreciseAsFileTime");
    if (pPrecise) {
      MH_CreateHook(
        (LPVOID) pPrecise, (LPVOID) &hkGetSystemTimePreciseAsFileTime, (LPVOID*) &oGetSystemTimePreciseAsFileTime
      );
    }
  }

  MH_EnableHook(MH_ALL_HOOKS);
  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
  }
  return TRUE;
}
