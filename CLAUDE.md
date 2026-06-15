# MinGeneralsmd — Community Fork of C&C Generals / Zero Hour (SAGE Engine)

> **Source:** <https://deepwiki.com/hunt1hunt/MinGeneralsmd> | <https://github.com/hunt1hunt/MinGeneralsmd>
>
> This file is a comprehensive project index and architectural guide, mirroring the content from DeepWiki.

---

## 1. Project Overview

**MinGenerals** is a community fork of *Command & Conquer Generals* and its expansion *Zero Hour* (referred to in code as **GeneralsMD**). This repository contains the complete **SAGE** (Strategy Action Game Engine) source code as it existed during development at EA Pacific, including the full engine, game logic, and tools required to build the RTS experience.

**Key architectural pillars:**
- **Deterministic lock-step architecture** — all simulation is deterministic across networked peers
- **Strict Simulation/Presentation separation** — `GameLogic` (server) and `GameClient` (presentation layer) are independent entities even in single-player
- **Data-driven design** — most game objects (`Thing`), weapons, and visual effects are defined in `.ini` files and loaded at runtime via the INI parser and `ThingTemplate` system
- **Fixed-rate simulation** — game runs at a fixed logic rate, typically 30 FPS

**Language:** C++ (89.1%), C (9.9%)
**License:** GNU General Public License v3.0 (with additional terms under GPLv3 Section 7 for EA and CommunityRTS)
**Status:** Early setup phase — focus is on build infrastructure, codebase cleanup, and restoring a working Windows build.

---

## 2. Repository Structure

```
MinGeneralsmd/
├── .gitattributes              # Git LFS/text/binary attributes
├── .github/                    # GitHub Actions CI/CD workflows
├── .gitignore                  # Filters VS/VS Code/JetBrains artifacts, build outputs
├── LICENSE.md                  # GPLv3 with additional Section 7 terms
├── README.md                   # Project overview and status
│
├── Generals/                   # Original C&C Generals source
│   └── Code/
│       ├── GameEngine/         # Core engine library (DSP project)
│       │   ├── GameEngine.dsp  # VC++ 6.0 project file
│       │   ├── Include/        # Public headers
│       │   └── Source/         # Implementation
│       ├── GameEngineDevice/   # Rendering/audio device (DSP project)
│       ├── Libraries/          # Internal helpers
│       ├── Main/               # WinMain entry point, resources
│       ├── Tools/              # Editor tools
│       └── RTS.dsp / RTS.dsw   # Root workspace (VC++ 6.0)
│
├── GeneralsMD/                 # Zero Hour expansion (Mission Disk) source
│   └── Code/
│       ├── GameEngine/         # Core engine library
│       │   ├── GameEngine.dsp
│       │   ├── Include/
│       │   │   ├── Common/         # Core engine headers (137 files)
│       │   │   ├── GameClient/     # Presentation layer headers
│       │   │   ├── GameLogic/      # Simulation layer headers
│       │   │   ├── GameNetwork/    # Multiplayer headers
│       │   │   └── Precompiled/    # PCH setup
│       │   └── Source/
│       │       ├── Common/         # Core engine source
│       │       ├── GameClient/     # Presentation layer source
│       │       ├── GameLogic/      # Simulation layer source
│       │       ├── GameNetwork/    # Multiplayer source
│       │       └── Precompiled/    # PCH source
│       ├── GameEngineDevice/   # Rendering/Audio device abstraction
│       │   ├── GameEngineDevice.dsp
│       │   ├── Include/
│       │   │   ├── W3DDevice/         # W3D (Westwood 3D) renderer
│       │   │   ├── MilesAudioDevice/  # Miles Sound System
│       │   │   ├── VideoDevice/       # Video playback
│       │   │   └── Win32Device/       # Win32 platform layer
│       │   └── Source/ (same structure)
│       ├── Libraries/          # Internal code libraries
│       │   ├── Include/ (Granny, Lib, MSS)
│       │   ├── Lib/            # Pre-built static libraries
│       │   └── Source/
│       ├── Main/               # Application entry point
│       │   ├── WinMain.cpp         # ~36KB
│       │   ├── WinMain.h
│       │   ├── RTS.rc
│       │   ├── buildVersion.h
│       │   ├── generatedVersion.h
│       │   ├── resource.h
│       │   └── Generals.ico
│       ├── Tools/
│       │   ├── NVASM/             # NVIDIA shader assembler
│       │   └── WorldBuilder/      # MFC-based map editor
│       └── RTS.dsp / RTS.dsw
│
└── libraries/                  # Third-party dependencies
    ├── dxsdk/                  # DirectX SDK (D3D 8/9)
    ├── gamespy/                # GameSpy SDK (online multiplayer)
    └── stlport/                # STLport (cross-platform STL)
```

---

## 3. Build System

### Build Toolchain
- **Microsoft Visual C++ 6.0** — original development environment (`.dsp` / `.dsw` project files)
- **Platform:** Windows (Win32) only
- **Languages:** C++ (89%), C (10%), with NVASM shader assembly

### Key Project Files

| File | Purpose |
|------|---------|
| `RTS.dsw` | Root workspace linking GameEngine + GameEngineDevice |
| `GameEngine.dsp` | Core engine library (114KB project file) |
| `GameEngineDevice.dsp` | Rendering/audio device (43KB) |
| `WinMain.cpp` | Application entry point (~36KB) |

### CI/CD
- GitHub Actions configured
- Enforces **Conventional Commits**
- Build output staging: `Run/`, `Bin/`, `Lib/`, `Obj/`, `Build/`
- Build variants: `Release/`, `Debug/`, `Max4Release/`, `Max4Hybrid/`, `DebugW3D/`, `Hybrid/`
- Localization outputs: `English/`, `French/`, `German/`, `Spanish/`

---

## 4. Core Engine Architecture

### 4.1 Subsystem Pattern

The engine uses a **Subsystem pattern**. `GameEngine` (abstract) is specialized for Win32, acting as coordinator for:

```
GameEngine (abstract)
  └── GameEngineWin32
        ├── GameLogic     — deterministic simulation ("server")
        ├── GameClient    — rendering, audio, GUI ("presentation")
        ├── GameNetwork   — multiplayer connectivity
        └── SubsystemInterface — contract implemented by all subsystems
```

**Key files:**
- `Include/Common/GameEngine.h` — core engine class, `execute()` loop, subsystem lifecycle
- `Include/Common/GameCommon.h` — shared constants (fixed FPS=30, globals)
- `Include/Common/SubsystemInterface.h` — subsystem contract (init/reset/update/shutdown)

### 4.2 Application Bootstrap

```
WinMain (WinMain.cpp)
  └── GameMain()
        └── TheGameEngine singleton
              └── TheGameEngine->execute()  [main game loop]
```

### 4.3 Key Technical Concepts

| Concept | Description |
|---------|-------------|
| **Deterministic Simulation** | Game runs at fixed 30 FPS; all state updates in GameLogic |
| **Client/Server Split** | Even in SP, GameLogic=server, GameClient=presentation; client sends NetCommands to logic |
| **Data-Driven Design** | Things, weapons, VFX defined in `.ini` files, loaded as ThingTemplate |
| **Lock-Step Networking** | All peers execute identical simulation frames; only player inputs transmitted |

### 4.4 Memory Management
- `GameMemory.h` — custom allocators and memory pools
- `RAMFile.h` — RAM-backed file abstraction
- `Snapshot.h` — memory state snapshots

### 4.5 File System & Asset Pipeline
- `FileSystem.h`, `ArchiveFileSystem.h` — virtual file system abstraction
- `ArchiveFile.h`, `StreamingArchiveFile.h` — `.big` archive file support
- `LocalFile.h`, `LocalFileSystem.h` — native OS file I/O
- `Directory.h` — directory enumeration

### 4.6 INI Data-Driven Configuration
- `INI.h` — INI file parser (templates, game data, map settings)
- `INIException.h` — parsing error handling
- `WellKnownKeys.h` — predefined INI key constants

---

## 5. Simulation Layer (GameLogic)

Redirects: `Include/GameLogic/` and `Source/GameLogic/`.

### 5.1 Object & Thing System

| File | Description |
|------|-------------|
| `Thing.h` | Base game object (everything in the world is a Thing) |
| `ThingTemplate.h` | Template/blueprint from INI data |
| `ThingFactory.h` | Factory pattern for creating Things |
| `ThingSort.h` | Spatial sorting utilities |
| `ObjectStatusTypes.h` | Status effect type definitions |
| `KindOf.h` | Kind-of classification flags system |
| `Module.h` | Modular behavior system (Update, Die, Contain modules) |
| `ModuleFactory.h` | Module factory |
| `ClientUpdateModule.h` | Client-side module update |
| `DrawModule.h` | Visual/draw module interface |
| `StateMachine.h` | State machine framework (FSM for object behaviors) |
| `DisabledTypes.h` | Disabled/disabled state types |
| `Overridable.h`, `Override.h` | Override system for modular behavior |

### 5.2 Weapons, Armor & Combat

| File | Description |
|------|-------------|
| `DamageFX.h` | Damage effects definitions |
| `SpecialPower.h` | Special power/ability definitions |
| `SpecialPowerType.h` | Special power type enum |
| `SpecialPowerMaskType.h` | Special power mask |
| `Science.h` | Science/technology definitions |
| `Upgrade.h` | Upgrade system |
| `ProductionPrerequisite.h` | Build prerequisites |

### 5.3 AI & Pathfinding

| File | Description |
|------|-------------|
| `AI.h` | Main AI system (38KB header) |
| `AIDock.h` | AI docking/garrison logic |
| `ActionManager.h` | Action/order queue management |
| `BuildAssistant.h` | AI build assistance |
| `PartitionSolver.h` | Spatial partitioning for AI |
| `DiscreteCircle.h` | Geometry/circle calculations |

### 5.4 Script Engine & Victory Conditions
- `FunctionLexicon.h` — script function definitions
- `QuickTrig.h` — quick/trigger system

### 5.5 Economy & Teams

| File | Description |
|------|-------------|
| `Player.h` | Player state and data |
| `PlayerList.h` | Multiplayer player list |
| `PlayerTemplate.h` | Player faction template |
| `Team.h` | Team/grouping definitions |
| `Money.h` | Resource/money system |
| `ResourceGatheringManager.h` | Resource gathering logic |
| `Energy.h` | Power/energy system |
| `ScoreKeeper.h` | Scoring system |

### 5.6 Terrain & Map Logic
- `Terrain.h`, `TerrainTypes.h` — terrain system
- `MapObject.h`, `MapReaderWriterInfo.h` — map representation
- `Radar.h` — radar/minimap logic

---

## 6. Presentation Layer (GameClient)

Redirects: `Include/GameClient/` and `Source/GameClient/`.

### 6.1 GUI & Window System
- `Anim2D.h` — 2D animation system
- `AnimateWindowManager.h` — window animation management

### 6.2 Display, Fonts & Localization
- `Language.h` — localization / string tables
- `UnicodeString.h`, `AsciiString.h` — string handling

### 6.3 Input & Command Translation
- `CommandLine.h` — command-line argument parsing

### 6.4 Drawable & Visual Effects
- `DrawModule.h` — draw module interface
- `ModelState.h` — model state management
- `GameLOD.h` — level-of-detail management

### 6.5 Audio System

| File | Description |
|------|-------------|
| `GameAudio.h` | Audio system interface |
| `AudioEventRTS.h` | Game audio events |
| `AudioEventInfo.h` | Audio event configuration |
| `AudioSettings.h` | Audio configuration |
| `AudioAffect.h` | Audio effects (reverb, etc.) |
| `AudioRandomValue.h` | Randomized audio parameters |
| `AudioRequest.h` | Audio playback requests |
| `DynamicAudioEventInfo.h` | Dynamic audio event data |
| `MiscAudio.h` | Miscellaneous audio |
| `GameMusic.h` | Music system |
| `GameSounds.h` | Sound effects |
| `GameSpeech.h` | Speech/voice system |

---

## 7. Rendering & Device Layer (GameEngineDevice)

### 7.1 W3D Device (Westwood 3D Renderer)
- Direct3D 8/9 based (uses `libraries/dxsdk/`)
- Shader support via NVASM (NVIDIA vertex/pixel shader assembler)
- `W3DDevice/Include/` + `W3DDevice/Source/`

### 7.2 Miles Audio Device
- Wraps Miles Sound System
- 3D positional audio, streaming, playback
- `MilesAudioDevice/Include/` + `MilesAudioDevice/Source/`

### 7.3 Video Device
- Video playback subsystem
- `VideoDevice/Include/` + `VideoDevice/Source/`

### 7.4 Win32 Device
- Platform abstraction: window creation, input, timer, registry access
- `Win32Device/Include/` + `Win32Device/Source/`
- Key headers: `Registry.h`, `OSDisplay.h`, `SystemInfo.h`, `CriticalSection.h`

---

## 8. Networking Layer (GameNetwork)

### 8.1 Lock-Step Architecture

All peers run the same deterministic simulation. Only player commands are transmitted as `NetCommand` objects.

```
User Input → NetCommandMsg → FrameDataManager → NetworkTransport → Remote Peer
```

### 8.2 Key Network Files

| File | Description |
|------|-------------|
| `NetworkInterface.h` | Abstract network transport interface |
| `FrameData.h`, `FrameDataManager.h` | Frame-level data synchronization |
| `FrameMetrics.h` | Network performance metrics |
| `NetCommandMsg.h` | Command message types (16KB header) |
| `NetCommandList.h`, `NetCommandRef.h` | Command list management |
| `NetPacket.h` | Network packet format (12KB header) |

### 8.3 Connection & Transport

| File | Description |
|------|-------------|
| `Connection.h`, `ConnectionManager.h` | Connection lifecycle |
| `DisconnectManager.h` | Disconnect handling |
| `Transport.h` | Transport layer abstraction |
| `udp.h` | UDP socket implementation |
| `IPEnumeration.h` | Network interface enumeration |
| `FirewallHelper.h` | UPnP / NAT traversal |
| `NAT.h` | NAT negotiation |

### 8.4 GameSpy Online Services

| File | Description |
|------|-------------|
| `GameSpy/` (dir) | GameSpy SDK integration headers |
| `GameSpyGP.h` | Game presence / authentication |
| `GameSpyChat.h` | Chat/lobby functionality |
| `GameSpyGameInfo.h` | Game listing information |
| `GameSpyOverlay.h` | In-game overlay |
| `GameSpyThread.h` | Async GameSpy operations |
| `GameInfo.h` | Game session information (13KB) |

### 8.5 LAN & Skirmish

| File | Description |
|------|-------------|
| `LANAPI.h` | LAN discovery protocol (19KB header) |
| `LANAPICallbacks.h` | LAN callbacks |
| `LANGameInfo.h` | LAN game info |
| `LANPlayer.h` | LAN player representation |

### 8.6 Ranked & Matchmaking
- `RankPointValue.h`, `LadderPreferences.h`, `QuickmatchPreferences.h`
- `SparseMatchFinder.h`, `CustomMatchPreferences.h`
- `MultiplayerSettings.h`, `SkirmishPreferences.h`

---

## 9. Core Library Headers (Common/ — 137 files)

### 9.1 Types & Utilities
- `AsciiString.h`, `UnicodeString.h` — string classes
- `BitFlags.h`, `BitFlagsIO.h` — type-safe flags
- `List.h` — custom list container
- `STLTypedefs.h` — STL typedefs
- `Dict.h` — dictionary/key-value store
- `Geometry.h` — 3D math/geometry
- `RandomValue.h` — random number generation
- `CRC.h`, `CRCDebug.h` — CRC checksums
- `BezFwdIterator.h`, `BezierSegment.h` — bezier utilities
- `Money.h` — resource/money type

### 9.2 Serialization (Xfer System)
- `Xfer.h`, `XferLoad.h`, `XferSave.h` — save/load framework
- `XferCRC.h`, `XferDeepCRC.h` — CRC verification
- `DataChunk.h` — chunked data I/O
- `Snapshot.h` — state snapshots
- `Recorder.h` — game recording/replay

### 9.3 Debugging & Error Handling
- `Debug.h` — debug asserts and logging
- `Errors.h` — error system
- `StackDump.h` — call stack dump
- `MiniLog.h` — lightweight logging

### 9.4 Performance Profiling
- `PerfMetrics.h` — performance metrics collection
- `PerfTimer.h` — high-resolution timing
- `GameLOD.h` — dynamic level of detail
- `UnitTimings.h` — timing constants

### 9.5 Platform & OS
- `CriticalSection.h` — thread synchronization
- `ScopedMutex.h` — RAII mutex
- `Registry.h` — Windows registry access
- `SystemInfo.h` — system capability detection
- `OSDisplay.h` — OS display info
- `CommandLine.h` — command line parsing
- `MessageStream.h` — inter-thread messaging

---

## 10. Third-Party Libraries

| Library | Path | Purpose |
|---------|------|---------|
| **DirectX SDK** | `libraries/dxsdk/` | Direct3D 8/9, DirectInput, DirectSound |
| **GameSpy SDK** | `libraries/gamespy/` | Online multiplayer: auth, lobby, NAT traversal |
| **STLport** | `libraries/stlport/` | Cross-platform STL (for VS 6.0 compatibility) |

---

## 11. Tools

### 11.1 WorldBuilder (Map Editor)
- `Tools/WorldBuilder/` — MFC-based application
- **Terrain & Painting** — heightmap editing, texture painting
- **Object Placement** — unit/building/scatter placement
- **Scripts & Triggers** — mission scripting
- **Map Export** — binary map file generation

### 11.2 NVASM (Shader Assembler)
- `Tools/NVASM/` — NVIDIA vertex and pixel shader assembler
- Compiles shader assembly for fixed-function and programmable pipeline

---

## 12. Glossary

| Term | Meaning |
|------|---------|
| **SAGE** | Strategy Action Game Engine |
| **GeneralsMD** | Internal code name for Zero Hour (Mission Disk) |
| **GameLogic** | Simulation layer (deterministic "server") |
| **GameClient** | Presentation layer (rendering, audio, GUI) |
| **Thing** | Base class for all game objects |
| **ThingTemplate** | Data-driven template for object creation |
| **Xfer** | Serialization system for save/load and networking |
| **W3D** | Westwood 3D renderer |
| **Miles** | Miles Sound System audio engine |
| **Granny** | Granny 3D animation system |
| **NetCommand** | Unit of player input transmitted over network |
| **Lock-Step** | Synchronization model where all peers simulate the same frames |
| **WorldBuilder** | MFC-based map editor tool |
| **Subsystem** | Engine component following SubsystemInterface contract |
| **INI** | Configuration file format driving data-defined game content |
| **GameSpy** | Online multiplayer SDK (presence, chat, matchmaking) |

---

## 13. Licensing

- **Primary:** GNU General Public License v3.0
- **Additional terms:** GPLv3 Section 7 for Electronic Arts and CommunityRTS
- **Copyright:** 2025 CommunityRTS
- **Disclaimer:** Independent community project, not affiliated with EA
- **Assets:** Not included — you must own the original games (EA App or Steam)

---

## 14. Reference Links

- **DeepWiki:** <https://deepwiki.com/hunt1hunt/MinGeneralsmd>
- **GitHub:** <https://github.com/hunt1hunt/MinGeneralsmd>
- **Discord:** <https://discord.gg/CRZDZEhR5p>
- **C&C Ultimate Collection (EA):** <https://www.ea.com/games/command-and-conquer/command-and-conquer-the-ultimate-collection>
- **C&C Ultimate Collection (Steam):** <https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/>
