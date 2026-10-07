# VKAudioPlayer

An ultra-lightweight, high-performance console audio player and streaming aggregator built with modern C++20 and Qt6.

Most desktop streaming apps today are bloated web wrappers that consume hundreds of megabytes of RAM and noticeable CPU cycles just to play an audio stream. VKAudioPlayer is designed with the opposite mindset: native, lean, and fast. It runs directly inside your terminal, takes only tens of megabytes of memory, and streams audio directly through RAM without ever dumping temporary files onto your disk.

---

## Why VKAudioPlayer?

### Performance and Zero Disk Churn
* Native C++20 and miniaudio: no Electron, no embedded web engines for audio playback, and no background Python interpreters.
* In-Memory Streaming: chunks from HLS and audio streams are received, demuxed, and pushed directly into ring buffers in memory, feeding the audio decoder with near-instant playback latency.
* Low resource footprint: ideal for running in the background during work, on development machines, or on low-power hardware.

### Unified Music Experience
Instead of switching between multiple apps and browser tabs, VKAudioPlayer aggregates your accounts into a single terminal window:
* VK Music
* Yandex Music
* SoundCloud
* YouTube Music
* Local library (offline database, search, and file playback)

### Custom Themes and Visual Styling
The player looks great out of the box, but you can tailor its colors to fit your terminal environment. Colors can be customized interactively inside the in-app Settings modal or directly edited in `config.ini`, `ultimaet.ini` and `ftxui.cfg`. You can define custom accent, background, and border colors using HEX (`#RRGGBB`) or RGB formats.

### Everyday Features
* Library management: like tracks, create custom playlists, delete tracks, and add songs directly to your personal streaming account.
* Terminal mouse support: you can navigate by keyboard or simply click buttons, drag volume and seek sliders, and scroll lists with the mouse wheel.
* Built-in audio processing: 10-band DSP equalizer with presets, automatic 48 kHz stream resampling, and seamless crossfade.
* Extra utilities: real-time animated spectrum visualizers (FFT), lyrics fetcher, playlist export to `.txt`, and song recognition via Shazam.

---

## Project Structure

The codebase is strictly modular. The core audio logic, networking, and streaming services are completely decoupled from any user interface, making it straightforward to add new frontends (such as desktop GUI or mobile ports).

```
src/
├── core/                  # Core application engine (100% UI-independent)
│   ├── ApplicationCore    # Main coordinator for services, lifecycle, and signals
│   ├── IUiController.h    # Abstract interface for any UI implementation
│   ├── api/               # Streaming service clients (VK, Spotify, SC, Yandex, YouTube)
│   ├── audio/             # Sound engine (miniaudio, DSP equalizer, resampler, playback state)
│   ├── auth/              # Account routing, OAuth, and multi-service unified search
│   └── playlist/          # Playlist queues, shuffle, repeat, and index navigation
├── services/              # Background subsystems
│   ├── config/            # Configuration service managing config.ini
│   ├── database/          # SQLite database and repositories (tracks, playlists, sessions)
│   ├── downloader/        # Local track saving with metadata tagging
│   ├── network/           # Ring-buffered in-memory network streamer
│   └── session/           # Playback session state save & restore
├── models/
│   └── Track.h            # Unified track data model
├── utils/                 # Low-level helpers
│   ├── parser/            # Stream demuxer (MPEG-TS, HLS, AES-128 decryption)
│   ├── path/              # Cross-platform data, cache, and config paths
│   └── logger/            # Thread-safe logging facility
├── ui/                    # UI implementations
│   ├── tui/               # Modern interactive terminal UI powered by FTXUI
│   │   ├── screens/       # NowPlayingScreen, SearchScreen
│   │   ├── components/    # Modals: Equalizer, Settings, Playlists, Help, Login
│   │   ├── modals/        # Category tabs inside Settings (Playback, Network, Cache, Theme)
│   │   └── core/          # Input router, bottom hints bar, command line
│   └── console/           # Classic line-based CLI and command dispatcher
└── libs/                  # Lightweight vendored C libraries
    ├── tiny-aes/          # AES-128 CBC decryption for protected HLS segments
    ├── minimp3/           # Single-header MP3 decoder
    └── fdk-aac/           # Fraunhofer AAC decoder
```

---

## Build Options and Runtime Modes

### CMake Build Flags
* `-DENABLE_FTXUI=ON` *(Default)*: Builds the full interactive Terminal UI using FTXUI.
* `-DENABLE_FTXUI=OFF`: Excludes FTXUI and `src/ui/tui/` entirely. Produces a minimal line-based CLI binary, or serves as the base for building GUI/mobile targets.

### CLI Runtime Modes
When launching the executable, you can choose which interface style to start:

| Flag | Mode | Description |
| :--- | :--- | :--- |
| `--tui` | Modern TUI *(Default)* | Fullscreen interactive terminal UI with panels, modals, mouse support, and equalizer. |
| `--cli` | Classic Console | Fullscreen terminal mode with animated visualizer (`UltimateRenderer`). |
| `--cli-light` | Minimal Line CLI | Super-lightweight line-based interface (`ConsoleRenderer`) with virtually zero CPU overhead. |

---

## Keyboard Shortcuts

All hotkeys work identically in both English and Russian keyboard layouts, so you do not need to switch system layouts to control playback.

### Global Shortcuts

|     Key (ENG)     |  Key (RU)  | Action                                                   |
| :---------------: | :--------: | :------------------------------------------------------- |
|      `Space`      |  `Space`   | Toggle Play / Pause                                      |
|     `↑` / `↓`     | `↑` / `↓`  | Adjust volume                                            |
|     `+` / `-`     | `+` / `-`  | Alternative volume control                               |
|     `←` / `→`     | `←` / `→`  | Seek backward / forward                                  |
| `Shift + I` / `i` | `Ш` / `ш`  | Show / hide bottom hotkey hint bar                       |
|    `[` or `]`     | `х` or `ъ` | Collapse / expand sidebar                                |
|        `V`        |    `М`     | Cycle visualizer style (Solid Bars / Silk Wave / Mirror) |
|        `/`        |    `.`     | Open command bar                                         |
|        `P`        |    `З`     | Open Playlist Manager                                    |
|        `E`        |    `У`     | Open Equalizer                                           |
|    `O` or `F2`    |    `Щ`     | Open Settings                                            |
| `?` or `F1` / `H` |    `Р`     | Open Help                                                |
|        `Q`        |    `Й`     | Quit application                                         |
|        `F`        |    `А`     | Switch to Search screen                                  |
|       `Esc`       |   `Esc`    | Close active modal                                       |

### Search Screen

| Key (ENG) | Key (RU) | Action                                              |
| :-------: | :------: | :-------------------------------------------------- |
|    `N`    |   `Т`    | Next track                                          |
|    `B`    |   `И`    | Previous track                                      |
|   `Tab`   |  `Tab`   | Switch mode (Global Online Search vs Local Library) |
|    `A`    |   `Ф`    | Add highlighted track to custom playlist            |
|    `L`    |   `Д`    | Like / add current track to favorites               |
|   `Esc`   |  `Esc`   | Return to Now Playing screen                        |

---

## Quick Commands

List of them You can see in Help Modal window (`?` or `F1` / `H` )

---

## Building from Source

### Prerequisites
* C++20 compliant compiler (MSVC 2022 v17.4+ on Windows, GCC 11+ or Clang 13+ on Linux).
* CMake 3.16+.
* Qt6 packages: `Core`, `Gui`, `Widgets`, `Network`, `Sql`, `Qml`, `WebView` (for authorization only), `Concurrent`.

### Windows Quick Build
Use the included helper script:

```cmd
:: Build Debug version
build_helper.bat debug

:: Build Release version
build_helper.bat release
```

### Manual CMake Build

```bash
# Configure (TUI enabled by default)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build --config Release --parallel

# Run
./build/VKAudioPlayer.exe
```

---

## License

This project is licensed under the **[MIT License](LICENSE)**.

You are free to use, modify, distribute, combine, and use the code in personal or commercial projects. The only requirement is that the original copyright notice and license text remain in all copies or substantial portions of the software.
