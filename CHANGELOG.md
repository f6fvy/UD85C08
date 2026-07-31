# Changelog

History of UD85C08's development steps. No formal version numbering yet
(no first version validated on real hardware yet): entries describe
progress in order.

## Step 9 — Review/hardening pass (GUI + Core)

- Added `UD85C08::ToString(ResultCode)` to `UD85C08_Types.h` (public,
  header-only): centralizes the code<->text mapping previously
  duplicated as a local function in `UD85C08_CLI`, and now also used by
  `UD85C08_GUI`'s error messages.
- `UD85C08_CLI`'s local `ResultCodeToString` now just forwards to it.
- GUI hardening: guarded against a double-click on Connect creating two
  overlapping `Controller` instances; guarded `IDC_COMBO_PORT` having no
  selection (`CB_ERR`) before reading its text; error message boxes now
  include the actual `ResultCode` instead of a generic message.
- Fixed a real bug found along the way: `OnBnClickedButtonConnect` was
  calling `PopulateModeCombo()` without passing the combo's current
  selection, silently resetting the Mode combo to "Normal" on every
  successful connect (defeating the saved-settings restore done in
  `OnInitDialog`).
- `MockSerialTransport` extended with `SetForcedError`/`ClearForcedError`,
  to simulate a genuine transport failure (e.g. USB cable unplugged) as
  opposed to a plain "no data yet" timeout.
- 5 new `Controller` self-tests: several mismatched responses arriving
  before a real timeout are discarded cleanly (no hang, no false match);
  a real transport error is reported as `TransportError` immediately
  rather than waiting out the full timeout like a `Timeout` would;
  `StartInputPolling` under a persistent transport error still raises
  `TransportError` events and `StopInputPolling` still returns promptly
  (no busy-loop / no hang). Total: 51/51 self-tests passing.

## Step 8 — Settings persistence (.ini)

- Added `Settings.h/.cpp` in `UD85C08_GUI`: loads/saves port, protocol,
  mode, delay, and I/O relation to `%APPDATA%\UD85C08\settings.ini`
  (a plain, human-readable/editable .ini file, not the Registry, as
  requested), via `Get/WritePrivateProfileString` with an explicitly
  built path rather than MFC's default registry-backed profile
  functions.
- Wired into `UD85C08_GUIDlg`: loaded and applied in `OnInitDialog`
  (falls back to defaults if the file/keys are missing or the saved COM
  port is no longer present), saved on a successful `Connect` (port +
  protocol) and again on `OnDestroy` (full current state).
- Deliberately paused work on input polling / the worker thread (former
  "point 7"): it depends on HEX response formats and relay-LED update
  semantics that are still open questions in `HARDWARE_ASSUMPTIONS.md`,
  better resolved once the physical board is available rather than
  guessed now.

## Step 7 — GUI wiring: Connect/Disconnect, per-channel buttons

- Wired `IDC_BUTTON_CONNECT`/`IDC_BUTTON_DISCONNECT` to `UD85C08::Controller`
  (construct with the selected protocol, `Connect`/`Disconnect`, lock the
  port combo and protocol radios while connected).
- Wired the 8 channel ON/OFF button pairs using `ON_CONTROL_RANGE` (two
  handlers total instead of one per button), relying on
  `IDC_BUTTON_RELAY_ON_1..8` / `IDC_BUTTON_RELAY_OFF_1..8` being
  contiguous resource ID ranges - documented explicitly in
  `GUI/README.md` since the resource editor does not protect this
  invariant and a broken range would fail silently.
- Per-channel button behaviour adapts to the selected mode (Normal shows
  both ON/OFF buttons; Momentary/Toggle/Latch/Delay show a single
  relabeled button), matching the design discussed for the dialog.
- Updated `GUI/README.md` (was still describing the pre-creation state,
  in French) to reflect the actual project and document the
  contiguous-ID requirement.

## Step 6 — CLedControl (GUI indicator)

- Added `CLedControl` (`GUI/UD85C08_GUI/LedControl.h/.cpp`), a minimal
  two-state LED indicator (gray = off, red = on) derived from `CStatic`,
  drawn with GDI (filled circle) and double-buffered to avoid flicker.
- Deliberately written from scratch rather than reusing an old CodeGuru
  `CLed` class the user had used before: that class's header only carried
  a bare "All rights reserved" copyright notice with no explicit license
  grant, which is too ambiguous to keep in a project intended to
  eventually go public. A richer, more general-purpose LED control
  (multiple shapes/colors) may be written later as a separate discussion/
  module if needed; this one intentionally stays minimal for the current
  GUI's actual requirements.
- Lives in `UD85C08_GUI` (not `Core`), consistent with the "no GUI
  components in Core" rule.

## Step 5 — Git setup, third-party license compliance

- Initialized the Git repository (kept private for now, may become public
  once the project is validated on real hardware).
- `Core/third_party/naughter/SerialPort.h` excluded from version control
  (see `.gitignore` and `Core/third_party/naughter/README.md`): the
  author's license only allows redistributing the exact, unmodified
  version he releases, to keep a single distribution point for the
  source code. This is handled from the very first commit, so there is
  no history to rewrite later before making the repository public.

## Step 4 — Documentation (README / CHANGELOG / HARDWARE_ASSUMPTIONS)

- Rewrote the root README to reflect the project's actual state (no longer
  just a bare skeleton).
- Created this CHANGELOG.
- Created `HARDWARE_ASSUMPTIONS.md`: centralized every currently-assumed
  point (never verified against the physical board), previously scattered
  across comments in several files.

## Step 3 — Testable Controller + full CLI

- `ISerialTransport` and `MockSerialTransport` promoted from `Core/src/`
  (internal) to `Core/include/` (public API): these are our own
  abstractions (no CSerialPort2/ATL dependency exposed), and making them
  public lets `Controller` be tested without hardware, and lets any
  external consumer do the same in its own tests.
- Added a second constructor `Controller(ProtocolMode, unique_ptr<ISerialTransport>)`
  for transport injection.
- Added 8 `Controller` + `MockSerialTransport` integration tests to
  `SelfTests.cpp` (HEX/AT frames sent verified byte by byte, queries with
  a simulated response, real timeout, error before connecting).
- Rewrote `UD85C08_CLI`: full argument parsing (`--port`, `--protocol`,
  every command in the protocol, `--monitor-inputs`), ready to drive a
  real board as soon as one is available.
- Total: 46/46 self-tests passing (validated on both Linux and VS 2022).

## Step 2 — AT / HEX codecs + Controller (first version)

- Programmatically verified the HEX checksum formula
  (`sum of the first 7 bytes modulo 256`) against every worked example in
  the datasheet, instead of hand-copying the value tables.
- Implemented `AtCommandCodec` (ASCII encoding + line-by-line decoding)
  and `HexFrameCodec` (8-byte frame encoding/decoding).
- Implemented `ISerialTransport` (interface), `SerialTransport` (real
  implementation backed by `CSerialPort2`), and `MockSerialTransport`
  (fake transport, still internal at this point).
- Implemented `Controller`: synchronous commands (`Send*`), blocking
  queries with a timeout (`Query*`), a background thread polling the
  inputs and dispatching events through a callback.
- First self-tests (38 tests, codecs only) via `RunSelfTests()`, callable
  from the CLI without any hardware.
- All code comments switched to English from this step onward (convention
  adopted for the rest of the project).

## Step 1 — Visual Studio project setup

- Defined the 3-project architecture: `UD85C08` (Core, static library),
  `UD85C08_CLI` (console), `UD85C08_GUI` (MFC), grouped in a single
  `UD85C08.sln` solution.
- Decided to reuse `CSerialPort2` (naughter.com) instead of reimplementing
  low-level serial port access.
- Naming convention adopted: `UD85C08_` prefix (specific to this board
  rather than a generic "Relay" name), `_` separator.
- Strict separation between public API (`Core/include/`) and internal
  implementation (`Core/src/`), in anticipation of use by a future
  external project (see below) with no dependency on MFC.
- Created the projects in Visual Studio 2022 (Static Library, Console App,
  MFC App wizards), configured MFC/ATL, disabled precompiled headers on
  `Core`, set C++ Language Standard to `/std:c++17` on all 3 projects
  (required for the structured bindings used in the self-tests).
- Initial skeleton that compiles (empty files / TODOs), with no functional
  logic yet.

## Context notes (decisions made, not tied to a specific code step)

- A third project (MFC, driving 4 relays via UDP frames and mouse clicks,
  continuously reading 4 inputs) is planned but **explicitly set aside**:
  it will come well after `UD85C08_CLI` / `UD85C08_GUI`, in a **separate
  Visual Studio solution**, consuming only the published artifacts of
  `UD85C08_Core` (`dist/lib` + `dist/include`), with no dependency on the
  source code or on `UD85C08_GUI`.
- No GUI components in `UD85C08_Core`: all UI (MFC or otherwise) stays in
  the consumer projects.
- No physical UD85C08 board was available during steps 1 through 4: all
  development was validated through self-tests and `MockSerialTransport`
  only. See `HARDWARE_ASSUMPTIONS.md`.
