# Design Specification: Android Port & VK-Style Touch UI for VKAudioPlayer

**Date:** 2026-10-07  
**Status:** Draft  
**Target Platform:** Android (API 26+ / Android 8.0 - Android 15), x86_64 (emulator) & arm64-v8a (devices)

---

## 1. Objectives & Requirements

1. **Independent Build Setup:**
   - Dedicated build output directory (`cmake-build-android-x86_64` and `cmake-build-android-arm64`) completely separate from desktop build folders (`cmake-build-debug`, `cmake-build-release`).
   - One-click build script `build_android.bat` supporting target architectures (`x86_64` for PC emulators and `arm64_v8a` for physical devices) and build types (`debug` / `release`).

2. **Core & Platform Isolation:**
   - Adhere strictly to [AGENTS.md](file:///c:/others/Codes/audio%20player/AGENTS.md): zero WebEngine, zero Python runtime, in-memory audio streaming, native C++20 + Qt 6.
   - Guard desktop-only flags (AVX2, Shazam FFI DLL, Windows Crash Handler).
   - Link Android native audio and system libraries (`aaudio`, `OpenSLES`, `log`).
   - Fully integrate Qt 6 Android CMake pipeline (`qt_add_executable` and `qt_finalize_target`).

3. **VK-Style Touch UI (Qt Quick / QML):**
   - **Header:** Top search bar supporting search and tab navigation ("Моя музыка" / "Поиск") with drawer toggle button.
   - **Side Drawer (левое слайд-окно):** Selection of streaming providers (VK, SoundCloud, YouTube, Yandex, Spotify), auth status.
   - **Middle (Track List):** Fast scrollable `ListView` displaying album art / cover thumbnails, track titles, artist names, durations, and playing indicators.
   - **Bottom Tray (Mini-player):** Compact bottom bar showing currently playing track with quick Play/Pause button and tap gesture that expands into a full-screen Now Playing screen (with seek bar, full cover art, volume, next/prev, shuffle).
   - **Authentication:** Utilize native mobile flow (`VkAuthService` + `VkDeviceManager`) without WebViews.

---

## 2. Architecture & Components

### 2.1 Build Architecture (`build_android.bat` & `CMakeLists.txt`)

* **CMake Pipeline:**
  ```cmake
  if(ANDROID)
      set(ENABLE_FTXUI OFF CACHE BOOL "" FORCE)
      target_link_libraries(${PROJECT_NAME} PRIVATE aaudio OpenSLES log)
      set_target_properties(${PROJECT_NAME} PROPERTIES
          QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
      )
      qt_finalize_target(${PROJECT_NAME})
  endif()
  ```
* **Toolchain Execution:**
  - Script invokes `C:\Qt\6.11.1\android_<arch>\bin\qt-cmake.bat` passing:
    - `-DANDROID_SDK_ROOT="C:/Users/danya/AppData/Local/Android/Sdk"`
    - `-DANDROID_NDK_ROOT="C:/Users/danya/AppData/Local/Android/Sdk/ndk/30.0.16248370"`
    - `-DCMAKE_BUILD_TYPE=<Debug|Release>`
    - `-B cmake-build-android-<arch>`
  - Target: `${PROJECT_NAME}_apk` which builds the shared library, runs `androiddeployqt`, and executes Gradle to generate `VKAudioPlayer-debug.apk`.

### 2.2 C++ UI Controller Layer (`src/ui/android/`)

* **`AndroidUiController` (implements `IUiController`):**
  - Inherits `IUiController` and `QObject`.
  - Owned by `ApplicationCore` via `UiFactory::Create(UiMode::ANDROID, core)`.
  - Instantiates `QQmlApplicationEngine`, binds C++ backend to QML context.
  - Receives core callbacks (`OnTrackChanged`, `OnAudioFetched`, `SetStatusMessage`) and updates QML models asynchronously.

* **`AndroidPlayerBridge` (QObject exposed to QML):**
  - **Properties:**
    - `bool isPlaying` (NOTIFY)
    - `qint64 positionMs`, `qint64 durationMs` (NOTIFY)
    - `float volume` (NOTIFY)
    - `QString currentTitle`, `QString currentArtist`, `QString currentCoverUrl` (NOTIFY)
    - `QString statusMessage` (NOTIFY)
    - `QString currentSource` (NOTIFY)
    - `bool isDrawerOpen`
  - **Invokables / Slots:**
    - `void play()`, `void pause()`, `void togglePlay()`
    - `void seek(qint64 positionMs)`
    - `void nextTrack()`, `void prevTrack()`
    - `void setVolume(float v)`
    - `void search(const QString& query)`
    - `void playTrackAtIndex(int index)`
    - `void switchSource(const QString& sourceName)`
    - `void loginVk(const QString& login, const QString& password, const QString& code2fa)`

* **`TrackListModel` (`QAbstractListModel`):**
  - Roles: `title`, `artist`, `durationStr`, `coverUrl`, `source`, `isPlaying`.
  - Efficiently handles hundreds of tracks with smooth scrolling and zero lag.

### 2.3 QML UI Layer (`src/ui/android/qml/`)

* **`Main.qml`:**
  - Uses `QtQuick.Controls` Material or Basic lightweight style.
  - **Left Drawer (`Drawer`):**
    - Sliding sidebar listing services: VKontakte, YouTube, SoundCloud, Yandex Music.
    - Shows account info / login button.
  - **Top Navigation Bar:**
    - Menu button (opens Drawer).
    - Search field with clear button and live search trigger.
    - Category pill switches ("Все треки" / "Поиск").
  - **Center View (`ListView`):**
    - Delegate: row with rounded cover image, title & artist text, duration, play indicator.
    - Tap on item -> calls `player.playTrackAtIndex(index)`.
  - **Bottom Mini-Player Tray:**
    - Persistent bottom bar (height ~64dp).
    - Miniature cover, title/artist marquee or elided text, Play/Pause icon button.
    - Progress line on the top border.
    - Swipe up or tap opens the expanded `NowPlayingSheet.qml`.
  - **Now Playing Modal (`NowPlayingSheet.qml`):**
    - Full-screen or bottom-sheet view.
    - Large high-resolution cover image.
    - Track title and artist with lyrics preview button.
    - Interactive seek slider with current time and remaining time.
    - Control bar: Shuffle, Previous, Play/Pause, Next, Repeat.

---

## 3. Android Platform Integration

* **`android/AndroidManifest.xml`:**
  - Package: `com.vkaudioplayer.app`
  - Permissions:
    - `android.permission.INTERNET`
    - `android.permission.ACCESS_NETWORK_STATE`
    - `android.permission.WAKE_LOCK`
    - `android.permission.FOREGROUND_SERVICE`
    - `android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK`
  - Activity: `org.qtproject.qt.android.bindings.QtActivity` with `android:screenOrientation="portrait"` or sensor portrait.

* **Audio Lifecycle:**
  - `MiniaudioEngine` uses Android AAudio low-latency audio stream.
  - Buffer sizing calibrated for AAudio streams to prevent glitches during lock screen / background state.

---

## 4. Verification Plan

1. **CMake Configuration Verification:**
   - Run `build_android.bat x86_64 debug`.
   - Verify that build directory `cmake-build-android-x86_64` is created and CMake configuration finishes with 0 errors.

2. **Compilation & APK Generation:**
   - Verify compilation of all C++20 core and Android UI sources.
   - Verify generation of `VKAudioPlayer-debug.apk` in `cmake-build-android-x86_64/android-build/build/outputs/apk/debug/`.

3. **Emulator Deployment & Functional Test:**
   - Launch AVD `Medium_Phone_API_36` via `emulator.exe`.
   - Install APK: `adb install -r <apk_path>`.
   - Launch App: `adb shell am start -n com.vkaudioplayer.app/org.qtproject.qt.android.bindings.QtActivity`.
   - Verify UI rendering (header search, left drawer, track list, bottom tray).
   - Perform search on SoundCloud / YouTube.
   - Play a stream, verify `miniaudio` logs in logcat (`AAudio output stream opened successfully`), and confirm playback.
