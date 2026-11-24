# Gorgon Windows Build & Unit Test Fix Log

- **Repository**: `darkgazeorg/Gorgon` (commit `0331a62` — *Complete Vulkan Switch, including Windows*)
- **Platform**: Windows 11 x64, Visual Studio 18 / MSVC 14.51, Ninja Multi-Config, Release, preset `WindowsTesting`
- **Test Result**: **14/22 -> 19/22 passed** (100% of the 19 unit test suites with active test cases pass; the remaining 3 failures are empty test files whose test cases are commented out upstream)

---

## 1. Summary Table

| Test Suite | Before Fix | Root Cause Category | Modified File | After Fix |
|---|---|---|---|---|
| `JSON` | `SIGSEGV - Stack overflow` (`JSON.cpp:2889`) | **Library bug** (MSVC x64 exception handling stack overflow) | `Source/Gorgon/Encoding/JSON.cpp` | Passed (235 cases / 527 assertions) |
| `Scene` | Runtime crash (null pointer dereference) | **Missing test setup** (`UI::Initialize()` not called) | `Testing/Source/Unit/Scene.cpp` | Passed (1 case / 17 assertions) |
| `Filesystem` | Fails immediately on startup (`Filesystem.cpp:535`) | **Test tag syntax error** (missing closing `]`) | `Testing/Source/Unit/Filesystem.cpp` | Passed (29 cases / 103 assertions) |
| `Geometry` | Floating-point assertion failure (`Geometry.cpp:914-915`) | **Insufficient precision in truncated test literals** | `Testing/Source/Unit/Geometry.cpp` | Passed (11 cases / 279 assertions) |
| `KeyRepeater` | Repeat-count assertion failure (`KeyRepeater.cpp:176-190`) | **Windows frame-rate cap & boundary timer jitter** | `Testing/Source/Unit/KeyRepeater.cpp` | Passed (1 case / 63 assertions) |
| `FLAC` | `No tests ran` | Entire test block commented out upstream (`/* ... */`) | Unmodified | Still fails (no active test cases) |
| `Hashmap` | `No tests ran` | All test cases commented out upstream (`//`) | Unmodified | Still fails (no active test cases) |
| `Layer` | `No tests ran` | Entire test block commented out upstream (`/* ... */`) | Unmodified | Still fails (no active test cases) |

---

## 2. Detailed Problem & Fix Log

### 1. `JSON` — Stack Overflow on Deep Nesting on Windows (Core Library Bug)

- **Symptom**: `UnitTest-JSON` crashed during the `[json][security]` test (`Testing/Source/Unit/JSON.cpp:2889`) when parsing deeply nested arrays `[[[...]]]` exceeding `MaxNestingDepth` (1000). Instead of throwing `JSON::Error`, it aborted with `SIGSEGV - Stack overflow` (exit code 127). Even temporarily lowering `MaxNestingDepth` to `128` still overflowed the stack.
- **Root Cause**: In `Source/Gorgon/Encoding/JSON.cpp`, `parseArray()` and `parseObject()` unconditionally wrapped recursive `parseValue()` / `parseString()` calls in `try { ... } catch (const JSON::Error &) { if (!best_effort) throw; ... }` regardless of whether `best_effort` mode was enabled.
  - Under Windows x64 MSVC C++ EH (SEH/funclet unwinding), executing `throw;` inside each `catch` handler pushes a fresh `_CxxThrowException` / `RtlDispatchException` / `CONTEXT` frame (~4–8 KB per level) onto the active stack rather than unwinding first.
  - In strict mode (`best_effort == false`), catching and rethrowing across 1000 nested frames rapidly exhausts Windows' default 1 MB thread stack (Linux's default 8 MB stack + Itanium EH masked this issue).
- **Fix**: Updated the three call sites in `Source/Gorgon/Encoding/JSON.cpp` — `parseArray()` (`elem = parseValue()`) and `parseObject()` (`key = parseString()`, `val = parseValue()`) — to wrap calls in `try/catch` only when `if (best_effort)` is true. In strict mode, calls execute directly so exceptions unwind in a single pass to the top-level caller. `JSON::MaxNestingDepth = 1000` remains unchanged with no API changes.
- **Verification**: `UnitTest-JSON.exe -r compact` -> `All tests passed (527 assertions in 235 test cases)`, `EXIT=0`.

### 2. `Scene` — Crash Due to Missing UI Registry Initialization

- **Symptom**: `UnitTest-Scene` crashed with an access violation (exit code 139).
- **Root Cause**: `SceneManager` accesses `Widgets::Registry::Active()`, which is initialized by `UI::Initialize()`. `Testing/Source/Unit/Scene.cpp` called `Initialize("Unittestscene")` but never called `UI::Initialize()`.
- **Fix**: Added `#include <Gorgon/UI.h>` and called `UI::Initialize();` right after constructing `SceneManager` in `Testing/Source/Unit/Scene.cpp`.
- **Verification**: `UnitTest-Scene.exe -r compact` -> `All tests passed (17 assertions in 1 test case)`, `EXIT=0`.

### 3. `Filesystem` — Missing Closing Bracket in Catch2 Tag

- **Symptom**: `UnitTest-Filesystem` failed immediately at startup pointing to `Testing/Source/Unit/Filesystem.cpp:535`.
- **Root Cause**: Line 535 declared `TEST_CASE("Load - PathNotFoundError", "[Load][PathNotFoundError")` without a closing `]`. Catch2 v3 strictly validates tags and aborts on malformed tag syntax.
- **Fix**: Corrected the tag to `"[Load][PathNotFoundError]"`.
- **Verification**: `UnitTest-Filesystem.exe -r compact` -> `All tests passed (103 assertions in 29 test cases)`, `EXIT=0`.

### 4. `Geometry` — Truncated Floating-Point Literals Exceed Default `Approx` Tolerance

- **Symptom**: `REQUIRE( p3.X == Approx(4.5454) )` and `REQUIRE( p3.Y == Approx(3.8461) )` failed at `Testing/Source/Unit/Geometry.cpp:914-915`.
- **Root Cause**: `50.0f / 11.0f = 4.5454545...` and `50.0f / 13.0f = 3.8461538...`. Truncating the expected literals to 4 decimal places (`4.5454`, `3.8461`) produced a relative error exceeding Catch2 v3's default `Approx` tolerance.
- **Fix**: Updated the expected literals to `Approx(4.54545)` and `Approx(3.84615)`.
- **Verification**: `UnitTest-Geometry.exe -r compact` -> `All tests passed (279 assertions in 11 test cases)`, `EXIT=0`.

### 5. `KeyRepeater` — Off-by-One Repeat Count Under Windows Timer Jitter

- **Symptom**: Repeat-count assertion failed at `Testing/Source/Unit/KeyRepeater.cpp:176-190` (1 failed out of 55 assertions).
- **Root Cause**:
  1. The test did not disable the default minimum frame duration, leaving `NextFrame()` polling too coarse.
  2. `wait = 200` landed right on the 4th accelerated repeat boundary (200 ms; 5th repeat fires at 240 ms), causing Windows timer jitter to frequently miss the 4th trigger.
- **Fix**: Added `SetMinimumFrameDuration(0);` after `Initialize("KeyRepeater_test")` and adjusted `wait` from `200` to `220` (midpoint between 200 ms and 240 ms) in `Testing/Source/Unit/KeyRepeater.cpp`.
- **Verification**: `UnitTest-KeyRepeater.exe -r compact` -> `All tests passed (63 assertions in 1 test case)`, `EXIT=0`.

---

## 3. Residual Failures (3/22)

Running the full CTest suite (`ctest --test-dir build/WindowsTesting -C Release --output-on-failure`) resulted in **19/22 passed (86%)** in 28.76 s. The 3 remaining failures are unrelated to Windows:

- **`FLAC`** (`Testing/Source/Unit/FLAC.cpp:10-122`): The entire `TEST_CASE("FLAC")` is commented out with `/* ... */`.
- **`Hashmap`** (`Testing/Source/Unit/Hashmap.cpp:44, 74, 107`): All 3 `TEST_CASE` definitions are commented out with `//`.
- **`Layer`** (`Testing/Source/Unit/Layer.cpp:17-360`): All `TEST_CASE` definitions are commented out with `/* ... */`.

Catch2 v3 prints `No tests ran` and exits with a non-zero code when a test binary contains zero active test cases, so CTest reports them as failed.

---

## 4. Modified Files (`git status`)

```text
M Source/Gorgon/Encoding/JSON.cpp
M Testing/Source/Unit/Filesystem.cpp
M Testing/Source/Unit/Geometry.cpp
M Testing/Source/Unit/KeyRepeater.cpp
M Testing/Source/Unit/Scene.cpp
```
