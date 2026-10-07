# Android Port & VK-Style Touch UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and package VKAudioPlayer for Android with a dedicated build pipeline and a lightweight VK-style Qt Quick / QML touch interface, ensuring seamless audio streaming without heavy dependencies.

**Architecture:** Maintain strict separation between core engine (`src/core/`) and the new mobile UI (`src/ui/android/`), using `IUiController` and a C++-QML bridge (`AndroidPlayerBridge` + `TrackListModel`). Android APK packaging is isolated into dedicated directories (`cmake-build-android-<arch>`) driven by `build_android.bat` and Qt 6 Android CMake tools.

**Tech Stack:** C++20, Qt 6.11.1 (Android ARM64 & x86_64), Qt Quick / QML, miniaudio (AAudio backend), Android NDK 30.0.16248370, Ninja, Gradle.

**Spec:** [docs/superpowers/specs/2026-10-07-android-port-and-ui-design.md](file:///c:/others/Codes/audio%20player/docs/superpowers/specs/2026-10-07-android-port-and-ui-design.md)

## Global Constraints

- Low resource footprint: Strictly ZERO `QtWebEngine`, ZERO embedded Python.
- In-memory streaming: All playback goes straight from network ring buffers into `miniaudio` decoder without writing audio files to storage.
- UI isolation: All Android interface code must reside strictly in `src/ui/android/`.
- Platform isolation: Desktop build directories (`cmake-build-debug`, `cmake-build-release`) must never be modified or polluted; Android builds reside strictly in `cmake-build-android-<arch>`.
- Audio backend: Uses native Android AAudio / OpenSL ES through `miniaudio`.
- VK auth: Native Android API via `VkAuthService` + `VkDeviceManager` (no browser/WebView).

## Review Focus

1. **AVX2 / MSVC Desktop Flags on Android:** Clang compiler on Android must not receive `/arch:AVX2` or `-mavx2`.
2. **Missing JNI / Android Libraries in Audio Callback:** `aaudio`, `OpenSLES`, and `log` must be explicitly linked on Android.
3. **Large Track Lists in QML ListView:** Memory footprint must stay low with image asynchronous loading and delegate recycling.
4. **Android Background Playback:** Foreground service and wake lock permissions must be declared in `AndroidManifest.xml`.
5. **Desktop Regression:** Desktop MSVC build (`build_helper.bat debug`) must continue to compile with 0 errors.

---

### Task 1: Android Build System Scaffolding (`build_android.bat`, `AndroidManifest.xml`, `CMakeLists.txt`)

**Files:**
- Create: `build_android.bat`
- Create: `android/AndroidManifest.xml`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `build_android.bat <arch> <debug|release>` which configures and builds `VKAudioPlayer_apk` target in `cmake-build-android-<arch>/`.
- Produces: Validated `AndroidManifest.xml` with `INTERNET`, `WAKE_LOCK`, and `FOREGROUND_SERVICE` permissions.

- [ ] **Step 1: Create `android/AndroidManifest.xml`**
  - Package: `com.vkaudioplayer.app`
  - Permissions: `android.permission.INTERNET`, `android.permission.WAKE_LOCK`, `android.permission.FOREGROUND_SERVICE`, `android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK`.
  - Activity: `org.qtproject.qt.android.bindings.QtActivity` with portrait orientation.

- [ ] **Step 2: Update `CMakeLists.txt` for Android target**
  - Guard `ENABLE_FTXUI`: force `OFF` if `ANDROID`.
  - Guard compiler flags: only apply AVX2 if `NOT ANDROID`.
  - Guard Windows libraries (`shazam_ffi.dll`).
  - Under `if(ANDROID)`:
    - Link `aaudio`, `OpenSLES`, `log`.
    - Set target property `QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"`.
    - Use `qt_add_executable` and call `qt_finalize_target(${PROJECT_NAME})`.

- [ ] **Step 3: Create `build_android.bat`**
  - Supports parameters: architecture (`x86_64` default for emulator, or `arm64_v8a`), build type (`debug` default).
  - Uses `C:\Qt\6.11.1\android_%ARCH%\bin\qt-cmake.bat`.
  - Passes `-DANDROID_SDK_ROOT="C:/Users/danya/AppData/Local/Android/Sdk"` and `-DANDROID_NDK_ROOT="C:/Users/danya/AppData/Local/Android/Sdk/ndk/30.0.16248370"`.
  - Output directory: `cmake-build-android-%ARCH%`.

- [ ] **Step 4: Verify CMake configuration for Android**
  - Run `build_android.bat x86_64 debug --config-only` (or configure step).
  - Expected: CMake generates build files in `cmake-build-android-x86_64` with 0 errors.

- [ ] **Step 5: Verify desktop build regression**
  - Run `build_helper.bat debug`.
  - Expected: Desktop MSVC build finishes with 0 errors (`ninja: no work to do`).

- [ ] **Step 6: Commit**
  - `git add android/ build_android.bat CMakeLists.txt`
  - `git commit -m "build(android): configure Android CMake pipeline and build script"`

---

### Task 2: Core Interface & Factory Extension (`UiMode::ANDROID` & `AndroidUiController`)

**Files:**
- Modify: `src/ui/IUiController.h`
- Modify: `src/ui/UiFactory.h`
- Modify: `src/ui/UiFactory.cpp`
- Modify: `src/main.cpp`
- Create: `src/ui/android/AndroidUiController.h`
- Create: `src/ui/android/AndroidUiController.cpp`

**Interfaces:**
- Consumes: `ApplicationCore`, `IUiController`, `UiMode`
- Produces: `class AndroidUiController : public IUiController` instantiated by `UiFactory::Create(UiMode::ANDROID, core)`.

- [ ] **Step 1: Add `UiMode::ANDROID` to `IUiController.h`**
  - Add enum value `ANDROID` to `enum class UiMode`.

- [ ] **Step 2: Create `src/ui/android/AndroidUiController.h` & `.cpp`**
  - Declare `class AndroidUiController : public IUiController`.
  - Override `Start()`, `Stop()`, `SetStatusMessage()`, `OnTrackChanged()`, `OnAudioFetched()`, `OnFinishedFetching()`.
  - Holds pointer to `ApplicationCore` and initializes `QQmlApplicationEngine`.

- [ ] **Step 3: Update `UiFactory.cpp` and `src/main.cpp`**
  - In `UiFactory::Create`: Handle `UiMode::ANDROID` returning `std::make_unique<AndroidUiController>(core)`.
  - In `src/main.cpp`: Under `#ifdef Q_OS_ANDROID`, set default `uiMode = UiMode::ANDROID`.

- [ ] **Step 4: Add new sources to `CMakeLists.txt`**
  - Add `src/ui/android/AndroidUiController.h` and `.cpp` under `if(ANDROID)`.

- [ ] **Step 5: Verify build**
  - Test compile desktop (`build_helper.bat debug`) to ensure no breaking changes.

- [ ] **Step 6: Commit**
  - `git add src/ui/IUiController.h src/ui/UiFactory.* src/ui/android/ src/main.cpp CMakeLists.txt`
  - `git commit -m "feat(ui): add AndroidUiController and integrate into UiFactory"`

---

### Task 3: C++ to QML Bridge & Track Model (`TrackListModel` & `AndroidPlayerBridge`)

**Files:**
- Create: `src/ui/android/TrackListModel.h`
- Create: `src/ui/android/TrackListModel.cpp`
- Create: `src/ui/android/AndroidPlayerBridge.h`
- Create: `src/ui/android/AndroidPlayerBridge.cpp`

**Interfaces:**
- Produces: `TrackListModel` (`QAbstractListModel`) with roles `TitleRole`, `ArtistRole`, `DurationRole`, `CoverUrlRole`, `SourceRole`, `IsPlayingRole`.
- Produces: `AndroidPlayerBridge` (`QObject`) exposed to QML as `player` context property or QML singleton.

- [ ] **Step 1: Implement `TrackListModel`**
  - Subclasses `QAbstractListModel`.
  - Method `setTracks(const std::vector<Track>& tracks)`.
  - Method `setPlayingIndex(int index)`.
  - Returns `title`, `artist`, `durationStr`, `coverUrl`, `source`, `isPlaying`.

- [ ] **Step 2: Implement `AndroidPlayerBridge`**
  - Exposes Q_PROPERTYs: `isPlaying`, `positionMs`, `durationMs`, `volume`, `currentTitle`, `currentArtist`, `currentCoverUrl`, `statusMessage`, `currentSource`.
  - Exposes Q_INVOKABLE methods:
    - `void play()`
    - `void pause()`
    - `void togglePlay()`
    - `void seek(qint64 ms)`
    - `void setVolume(float v)`
    - `void playTrackAtIndex(int index)`
    - `void search(const QString& query)`
    - `void switchSource(const QString& sourceName)`
    - `void nextTrack()`
    - `void prevTrack()`

- [ ] **Step 3: Connect `AndroidPlayerBridge` to `AndroidUiController`**
  - Connect callbacks from `ApplicationCore` (playback controller updates, track changed, audio fetched) to bridge properties and signals.

- [ ] **Step 4: Add sources to `CMakeLists.txt`**
  - Add `TrackListModel` and `AndroidPlayerBridge` to Android source list.

- [ ] **Step 5: Commit**
  - `git add src/ui/android/TrackListModel.* src/ui/android/AndroidPlayerBridge.* CMakeLists.txt`
  - `git commit -m "feat(ui): implement AndroidPlayerBridge and TrackListModel for QML"`

---

### Task 4: VK-Style Touch UI in QML (`Main.qml`, `NowPlayingSheet.qml`, resources)

**Files:**
- Create: `src/ui/android/qml/Main.qml`
- Create: `src/ui/android/qml/NowPlayingSheet.qml`
- Modify: `src/resources.qrc`

**Interfaces:**
- Consumes: `player` (`AndroidPlayerBridge`), `trackModel` (`TrackListModel`)
- Produces: Mobile touch UI loaded by `QQmlApplicationEngine` in `AndroidUiController`.

- [ ] **Step 1: Create `src/ui/android/qml/Main.qml`**
  - **Top Header:**
    - Hamburger button to open Drawer.
    - Search field with real-time text input and search submit button.
    - Segmented buttons / pills: "Мои треки" / "Поиск".
  - **Left Drawer (`Drawer`):**
    - List of available services: VKontakte, YouTube, SoundCloud, Yandex Music.
    - Selected service highlighted; tapping switches source via `player.switchSource(name)`.
    - Profile / Account info area.
  - **Middle Track List (`ListView`):**
    - Delegate displaying:
      - Rounded square album art (with fallback placeholder if empty).
      - Title (bold) and Artist (subtext).
      - Duration label (formatted mm:ss).
      - Animated / colored equalizer indicator when active track is playing.
    - Tap on track triggers `player.playTrackAtIndex(index)`.
  - **Bottom Mini-Player Tray:**
    - Height: 64dp, sticky at bottom.
    - Progress line across top border.
    - Mini cover art, Title and Artist.
    - Play / Pause button.
    - Tap or swipe up opens `NowPlayingSheet`.

- [ ] **Step 2: Create `src/ui/android/qml/NowPlayingSheet.qml`**
  - Full-screen or modal sheet:
    - Down arrow / close button to collapse back to mini-player.
    - Large high-resolution cover art with subtle drop shadow and rounded corners.
    - Title, Artist, and Source badge.
    - Seek bar slider with elapsed time and remaining time.
    - Control bar: Shuffle, Previous, Play/Pause, Next, Volume slider.

- [ ] **Step 3: Register QML files in `src/resources.qrc`**
  - Add `ui/android/qml/Main.qml` and `ui/android/qml/NowPlayingSheet.qml` to `qrc`.

- [ ] **Step 4: Update `AndroidUiController` to load `qrc:/ui/android/qml/Main.qml`**
  - Register `player` and `trackModel` in QML root context and load main QML file.

- [ ] **Step 5: Commit**
  - `git add src/ui/android/qml/ src/resources.qrc src/ui/android/AndroidUiController.cpp`
  - `git commit -m "feat(ui): implement VK-style QML interface for Android"`

---

### Task 5: Compilation, Packaging & Emulator Verification

**Files:**
- Output: `cmake-build-android-x86_64/android-build/build/outputs/apk/debug/VKAudioPlayer-debug.apk`

**Interfaces:**
- Produces: Installed, runnable Android APK on emulator `Medium_Phone_API_36`.

- [ ] **Step 1: Build Android APK**
  - Run `build_android.bat x86_64 debug`.
  - Verify that compilation and Gradle APK assembly succeed with 0 errors.

- [ ] **Step 2: Launch Android Emulator**
  - Start emulator `Medium_Phone_API_36` via `emulator.exe -avd Medium_Phone_API_36`.
  - Wait for `adb` connection.

- [ ] **Step 3: Install and Launch APK**
  - Install: `adb install -r <path_to_apk>`.
  - Launch: `adb shell am start -n com.vkaudioplayer.app/org.qtproject.qt.android.bindings.QtActivity`.

- [ ] **Step 4: Verify Runtime & Functionality**
  - Check logcat: confirm `MiniaudioEngine` successfully opened AAudio stream.
  - Perform test search (SoundCloud / YouTube / VK).
  - Tap track to play, verify stream playback and progress bar movement.
  - Test opening left Drawer to switch source.
  - Test expanding Now Playing sheet from bottom mini-player tray.

- [ ] **Step 5: Verify Desktop Regression**
  - Run `build_helper.bat debug`.
  - Confirm desktop Windows build remains 100% working and unaffected.

- [ ] **Step 6: Commit and Final Summary**
  - `git add .`
  - `git commit -m "feat(android): complete Android port with VK-style touch interface"`
