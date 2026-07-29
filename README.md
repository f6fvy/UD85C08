# UD85C08

Library and tools to drive the **UD85C08** USB relay/input board
(8DI-8DO, eletechsup / 485io.com) — 8 relay outputs, 8 digital inputs,
USB serial link (CH340 chip), dual protocol: AT (ASCII) or HEX
(8-byte frames).

See also:
- [`CHANGELOG.md`](CHANGELOG.md) — history of the project's steps
- [`HARDWARE_ASSUMPTIONS.md`](HARDWARE_ASSUMPTIONS.md) — **unverified
  assumptions about the real hardware**, to check first once the board
  is available

## Structure

```
UD85C08/
|-- UD85C08.sln                    <- VS solution grouping Core + CLI + GUI
|
|-- Core/                          <- project "UD85C08" -> builds UD85C08.lib
|   |-- include/                   <- PUBLIC API (the only thing an
|   |   |                             external consumer needs to include)
|   |   |-- UD85C08_Controller.h        Main entry point
|   |   |-- UD85C08_Types.h             Shared enums / structures
|   |   |-- UD85C08_IProtocolCodec.h    Common AT / HEX interface
|   |   |-- UD85C08_ISerialTransport.h  Transport interface (injectable)
|   |   |-- UD85C08_MockSerialTransport.h  Fake transport (tests)
|   |   `-- UD85C08_SelfTest.h          Self-tests (no hardware required)
|   |-- src/                       <- internal implementation
|   |   |-- AtCommandCodec.h/.cpp
|   |   |-- HexFrameCodec.h/.cpp
|   |   |-- SerialTransport.h/.cpp      Real implementation (CSerialPort2)
|   |   |-- UD85C08_Controller.cpp
|   |   `-- SelfTests.cpp
|   `-- third_party/naughter/      <- SerialPort.h (CSerialPort2, naughter.com)
|                                      NOT tracked in git - see its own README.md
|
|-- CLI/                           <- UD85C08_CLI.vcxproj (console)
|-- GUI/                           <- UD85C08_GUI.vcxproj (MFC)
|
`-- dist/                          <- published artifacts (lib + headers)
                                       for a future external project
```

## Current status

| Component | Status |
|---|---|
| `AtCommandCodec` / `HexFrameCodec` | Done, validated by 46 self-tests against the datasheet |
| `Controller` (logic, sync + polling) | Done, validated via `MockSerialTransport` |
| `SerialTransport` (real, `CSerialPort2`) | Written, compiles, **never tested on a physical port** |
| `UD85C08_CLI` | Functional (argument parsing, `--selftest`), hardware commands untested |
| `UD85C08_GUI` | MFC wizard skeleton only, no custom UI yet |

See `CHANGELOG.md` for step-by-step details, and especially
`HARDWARE_ASSUMPTIONS.md` before plugging in the board for the first time.

## Getting third-party dependencies

`Core/third_party/naughter/SerialPort.h` is NOT included in this
repository (license reasons, see `Core/third_party/naughter/README.md`).
Download it before building - instructions are in that file.

## Setting up in Visual Studio 2022 (already done, for reference)

1. Blank solution `UD85C08.sln` at the root
2. "Static Library" project `UD85C08` in `Core/` (MFC/ATL enabled in
   project properties, required by `third_party/naughter/SerialPort.h`,
   precompiled headers disabled, C++ Language Standard = `/std:c++17`)
3. "Console App" project `UD85C08_CLI` in `CLI/`, referencing `UD85C08`
4. "MFC App" project (Dialog based) `UD85C08_GUI` in `GUI/`, referencing
   `UD85C08`
5. `Additional Include Directories` = `$(SolutionDir)Core\include` on
   CLI and GUI
6. `C++ Language Standard` = `/std:c++17` on all 3 projects

## Running the self-tests

From a terminal in the output folder (e.g. `x64\Debug\`):

```
UD85C08_CLI.exe --selftest
```

Should print `46/46 tests passed.` — no hardware required.

## Next step

Wire `UD85C08_GUI` to `Controller`, then perform full validation on
physical hardware once available (see `HARDWARE_ASSUMPTIONS.md`).
