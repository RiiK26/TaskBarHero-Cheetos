# Deep Technical Analysis: Error 401 (BadUnauthorizedException) Caused by Speedhack

**Document ID:** DOC-ANALYSIS-2026-401  
**Target Application:** *TaskBarHero* (Unity 6000.0.72f1, IL2CPP x64)  
**Component Investigated:** `src/features/speedhack.cpp` (`speedhack.dll`)  
**Backend:** The Backend (BACKND / 뒤끝 - `thebackend.io`)  
**Status:** Resolved  

---

## 1. Executive Summary

When running *TaskBarHero* with `speedhack.dll` enabled at accelerated speeds, players intermittently encounter fatal server errors during stage completion or inventory synchronization:

```text
[CoreTBH_DebugLog] : [SERVER] Failed code 401 [1.1.4:bf2d800+]
[CoreTBH_DebugLog] : [SERVER] Failed code 401, Retry Count : 1 [1.1.4:bf2d800+]
[CoreTBH_DebugLog] : [SERVER] Failed code 401, Retry Count : 2 [1.1.4:bf2d800+]
[CoreTBH_DebugLog] : PopUpServerIssue : Error: 401\nBadUnauthorizedException\n [1.1.4:bf2d800+]
```

This error halts gameplay and presents a server communication error modal.

Our investigation traced this issue to `speedhack.cpp` intercepting Windows time APIs called by `GameAssembly.dll` (specifically `GetSystemTimeAsFileTime`, `GetSystemTimePreciseAsFileTime`, and `GetTickCount`). This artificial acceleration warps the client's internal wall-clock time (`DateTime.UtcNow`), leading directly to premature expiration of the game's 24-hour BACKND JWT `accessToken` and premature expiration of client-side HTTP retry windows (`RetryWindowExpired`).

---

## 2. Runtime Log Evidence

The error is documented in the game's Proton runtime logs (`Player-prev.log` lines 650–752):

```text
TaskbarHero.InventoryFunction:luv(Param)
TaskbarHero.StageClearFunction:GetParam()
qi:hnv(zd)

[CoreTBH_DebugLog] : [SERVER] Failed code 401 [1.1.4:bf2d800+]
qi:hnw()
qi:Update()
[CoreTBH_DebugLog] : [SERVER] Failed code 401, Retry Count : 1 [1.1.4:bf2d800+]
qi:hnw()
qi:Update()
[CoreTBH_DebugLog] : [SERVER] Failed code 401, Retry Count : 2 [1.1.4:bf2d800+]
s2grNdggDWH4ffEQlHi.<InvokeFunction>d__5`1:MoveNext()
UnityEngine.SetupCoroutine:InvokeMoveNext(IEnumerator, IntPtr)

[CoreTBH_DebugLog] : PopUpServerIssue : Error: 401\nBadUnauthorizedException\n [1.1.4:bf2d800+]
bex:olf(Boolean, Boolean, Boolean, String)
qi:hny(Int32, String, String)
```

By comparison, a clean session without time warping (`Player.log` lines 293–331) authenticates cleanly:
```text
[APPVERSION] Backend : 1.3.2, Client : 1.3.2
[TOKEN] length=234
[LOGIN] 201 : Success
[TIME] 2026-10-28T16:20:59.502Z : True , 200, Success
[PERSONAL SEED] 6
```

---

## 3. Architecture Breakdown

### 3.1 Backend Architecture (BACKND / 뒤끝)
* The game relies on **The Backend (뒤끝 / thebackend.io)** SDK (`namespace BackEnd` in `dump.cs`).
* Critical player milestones—such as stage clears and inventory rewards—are handled via **BACKND Function** (serverless cloud functions):
  * `TaskbarHero.StageClearFunction`
  * `TaskbarHero.InventoryFunction`
  * Called via `Backend.BFunc.InvokeFunction(string name, Param param)`.
* Every HTTP request to BACKND includes an `Authorization: Bearer <accessToken>` header.
* **Token Lifetime:** BACKND access tokens have a strict validity lifetime of **24 hours**.

### 3.2 Steam Web API Authentication
* In `class qi` (`TypeDefIndex: 673`), the client requests authentication tickets:
  ```csharp
  private const string bgcv = "SteamWebApiTicket";
  private HAuthTicket bgcw; // 0xF0
  private Callback<GetTicketForWebApiResponse_t> bgcx; // 0xF8
  ```
* These tickets are used to authenticate with BACKND and validate ownership against Steam's servers.

---

## 4. Root Cause Analysis

### 4.1 Root Cause A: `GetSystemTimeAsFileTime` Warps UTC Calendar Time (`DateTime.UtcNow`)
In `src/features/speedhack.cpp`:
```cpp
VOID WINAPI hkGetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
  if (!oGetSystemTimeAsFileTime) return;
  oGetSystemTimeAsFileTime(lpSystemTimeAsFileTime);

  uintptr_t caller = (uintptr_t) GET_CALLER();
  if (IsTargetCaller(caller)) {
    ...
    LONGLONG delta = (LONGLONG) (uli.QuadPart - g_Anchors.ftReal);
    uli.QuadPart = g_Anchors.ftFake + (ULONGLONG) ((double) delta * g_Anchors.speed);
    lpSystemTimeAsFileTime->dwLowDateTime  = uli.LowPart;
    lpSystemTimeAsFileTime->dwHighDateTime = uli.HighPart;
  }
}
```

* In IL2CPP, `System.DateTime.UtcNow` and `System.DateTime.Now` directly call `kernel32!GetSystemTimeAsFileTime`.
* When `speedhack.cpp` hooks `GetSystemTimeAsFileTime`, `DateTime.UtcNow` moves at `g_SpeedMultiplier` speed.
* **Impact Matrix:**
  | Configured Speed | Real Gameplay Time | Apparent Client Calendar Time Elapsed | Token Status |
  | :--- | :--- | :--- | :--- |
  | **5.0x** | 4.8 hours | **24.0 hours** | **Expired** (HTTP 401) |
  | **10.0x** | 2.4 hours | **24.0 hours** | **Expired** (HTTP 401) |
  | **20.0x** | 1.2 hours | **24.0 hours** | **Expired** (HTTP 401) |
  | **50.0x** | 28.8 minutes | **24.0 hours** | **Expired** (HTTP 401) |

Once the warped `DateTime.UtcNow` exceeds the 24-hour expiration threshold of the issued JWT access token, BACKND rejects subsequent `InvokeFunction` calls with:
`StatusCode: 401`, `ErrorCode: BadUnauthorizedException`, `Message: bad accessToken`.

### 4.2 Root Cause B: Overbroad Caller Filtering in `IsTargetCaller()`
In `speedhack.cpp`:
```cpp
inline bool IsTargetCaller(uintptr_t caller)
{
  if (caller >= g_UnityPlayerBase && caller < g_UnityPlayerEnd)
    return true;
  if (caller >= g_GameAssemblyBase && caller < g_GameAssemblyEnd)
    return true;
  return false;
}
```
* The author intended to restrict the hook to the game, but included **`GameAssembly.dll`**.
* `GameAssembly.dll` contains all managed C# assemblies compiled to native code:
  * `BackEnd.dll` (HTTP client, token manager, request queues)
  * `System.Net.Http.HttpClient`
  * `System.DateTime` (`get_UtcNow`)
  * `System.Environment` (`get_TickCount`)
  * `Steamworks.NET`
* Hooking calls originating from `GameAssembly.dll` corrupts the runtime environment of all C# network and time functions.

### 4.3 Root Cause C: Premature Timeout in Network Retry Window (`GetTickCount`)
Disassembly of `class qi` method `kao` (`RVA: 0x180E5A0C0`):
```nasm
0x180E5A0DF: call    0x18371bbb0            ; Environment.get_TickCount
0x180E5A0E4: movups  xmm0, [rbx + 0x7c]      ; last request tick count (bgcl)
0x180E5A0F6: call    ObscuredInt.op_Implicit
0x180E5A108: sub     edi, eax
0x180E5A10E: cvtdq2ps xmm0, xmm0
0x180E5A111: divss   xmm0, [rip + 0x3ef457f] ; divide by 1000.0f (ms -> sec)
0x180E5A119: addss   xmm0, xmm6
0x180E5A122: comiss  xmm7, xmm0
0x180E5A12A: seta    al                     ; returns true if RetryWindowExpired
```

* `kao` checks whether the elapsed time of an in-flight server request exceeds `bgci = 10` seconds (`RetryWindowExpired`).
* Because `GetTickCount` was accelerated 10x–20x, a 10-second timeout elapsed in **500 milliseconds to 1 second** of real internet time.
* If a physical round-trip request to BACKND took 1.2 seconds, the client timed out prematurely, aborted the request, burned through its 2 retries, and triggered `PopUpServerIssue`.

---

## 5. Why Unity Speedhacks Only Need `UnityPlayer.dll`

1. **Unity Engine Timing Loop:**
   * In Unity (IL2CPP x64), game speed, physics simulation (`FixedUpdate`), animation interpolation, particle simulation, and `Time.deltaTime` calculations run **inside `UnityPlayer.dll`**.
   * `UnityPlayer.dll` uses **`QueryPerformanceCounter`** (and `timeGetTime` on some builds) to measure frame deltas.
2. **C# Managed Scripts:**
   * Scripts in `GameAssembly.dll` access timing via `UnityEngine.Time::get_deltaTime()` and `UnityEngine.Time::get_time()`.
   * These are native internal calls (`icalls`) directly returning values already computed by `UnityPlayer.dll`.
   * C# code does **not** call `QueryPerformanceCounter` or `GetTickCount` to compute frame delta time.
3. **Calendar vs Monotonic Clocks:**
   * `GetSystemTimeAsFileTime` returns the UTC calendar date (100-nanosecond intervals since Jan 1, 1601).
   * No game engine uses wall-clock calendar time for physics or animation rendering.
   * Hooking `GetSystemTimeAsFileTime` provides **zero gameplay speedup** while breaking security tokens, SSL certificates, and calendar events.

---

## 6. Implementation Fix

To resolve Error 401 completely while preserving full speedhack functionality:

1. **Restrict `IsTargetCaller()` to `UnityPlayer.dll` only:**
   ```cpp
   inline bool IsTargetCaller(uintptr_t caller)
   {
     return (caller >= g_UnityPlayerBase && caller < g_UnityPlayerEnd);
   }
   ```
2. **Remove hooks on `GetSystemTimeAsFileTime` and `GetSystemTimePreciseAsFileTime`:**
   * Do not intercept or warp calendar time.
   * `System.DateTime.UtcNow` remains synchronized with real-world time.
3. **Preserve `QueryPerformanceCounter` & `timeGetTime` hooks for `UnityPlayer.dll`:**
   * Provides 100% of game speedup (animations, player movement, enemy spawns, physics) without side effects.
   * Network requests (`HttpClient`), Steam auth tickets, and BACKND tokens run on unmanipulated system time.
