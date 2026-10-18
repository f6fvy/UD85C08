# UD85C08

Library and tools to drive the **UD85C08** USB relay/input board
(8DI-8DO, eletechsup / 485io.com) — 8 relay outputs, 8 digital inputs,
USB serial link (CH340 chip), dual protocol: AT (ASCII) or HEX
(8-byte frames).

See also:
- [`CHANGELOG.md`](CHANGELOG.md) — history of the project's steps
- [`DEVICE_BEHAVIOR.md`](DEVICE_BEHAVIOR.md) — what the real board
  actually does, as measured on the bench (the codecs follow it)
- [`HARDWARE_ASSUMPTIONS.md`](HARDWARE_ASSUMPTIONS.md) — what is still
  unverified on the real hardware, and the pending GUI design decision

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
|
|-- GUI/                           <- UD85C08_GUI.vcxproj (MFC, Dialog based)
|   `-- UD85C08_GUI/
|       |-- UD85C08_GUIDlg.h/.cpp       Main dialog (wired to Controller)
|       |-- LedControl.h/.cpp           Custom CStatic-derived LED indicator
|       `-- Settings.h/.cpp             .ini persistence (%APPDATA%)
|
`-- dist/                          <- published artifacts (lib + headers)
                                       for a future external project
```

## Current status

| Component | Status |
|---|---|
| `AtCommandCodec` / `HexFrameCodec` | Done, validated by self-tests against the datasheet |
| `Controller` (logic, sync + polling) | Done, validated via `MockSerialTransport`, incl. hardening tests (mismatched responses, transport errors, persistent failures) |
| `SerialTransport` (real, `CSerialPort2`) | Works on a physical board (open, read, write, timeouts) |
| `UD85C08_CLI` | Functional; commands exercised on a real board in AT mode (open/close/read-relay/read-input/monitor) |
| `UD85C08_GUI` | Full UI wired to `Controller`: Connect/Disconnect, per-channel ON/OFF (mode-dependent), All ON/OFF, I/O relation, `.ini` settings persistence. Live relay/input LEDs (polling + `PostMessage`), input relation applied at Connect; validated on a real board |

109/109 self-tests passing (`AtCommandCodec`, `HexFrameCodec`, `Controller` +
`MockSerialTransport` integration and hardening, including the frames
actually captured from a real board). See `CHANGELOG.md` for step-by-step
details.

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

Should print `109/109 tests passed.` — no hardware required.

## Next step

The generic tools (`Core`, `UD85C08_CLI`, `UD85C08_GUI`) are validated on a
real board. What is left open is listed in `HARDWARE_ASSUMPTIONS.md`
(recovery after a USB unplug, Momentary/Delay fall-back reporting in HEX,
polling latency). The planned third project (Latch mode + input reading,
separate solution consuming `dist/`) is not started; the `dist/` publishing
step (post-build copy of `UD85C08.lib` and the public headers) still has to
be set up for it.
