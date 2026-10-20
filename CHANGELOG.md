# Changelog

History of UD85C08's development steps. No formal version numbering yet
(no first version validated on real hardware yet): entries describe
progress in order.

## Step 19 — Validation on the board, and publishing to `dist/`

- Validated with the real board and the GUI: connection lost (power cut or
  USB unplug) gives one alert and a clean return to "disconnected", and
  reconnecting works after Refresh; a port that is not the board's is
  refused at Connect; `--selftest` reports 109/109 under MSVC in Debug and
  Release. (GUI side: messages reworded, Refresh keeps the selected port.)
- **Post-build step for `dist/`** on the `UD85C08` (Core) project: copies
  `Core/include/*.h` to `dist/include/` and `UD85C08.lib` to
  `dist/lib/<Platform>/<Configuration>/` (two `xcopy` commands, see
  `README.md`). New `dist/README.md` explains the layout and what another
  solution must set and match (include/library directories, platform, C++17,
  MFC shared DLL / dynamic runtime).
- **`.gitignore`**: `dist/` is generated, so it is no longer versioned
  except its README (the old rules would have committed the `.lib` binaries
  and a second copy of the headers). Checked with `git check-ignore`.

## Step 18 — The board's power and its COM port; a documentation regression fixed

- Information from the bench: removing the board's power makes the COM
  port disappear from Windows (explained as the USB serial part being
  handled by the board's own microcontroller rather than a separate
  bus-powered chip; the datasheet mentions a CH340 - the observation is what
  matters). Consequences: a power cut is the same event as an USB
  unplug for the software; the "port open but board unpowered" case I had
  assumed does not exist. A `Timeout` from a first command or from polling
  now means "not the board's port" or "firmware not responding".
- Wording fixed in `HARDWARE_ASSUMPTIONS.md` (two open items rewritten),
  `DEVICE_BEHAVIOR.md` (new "Power" line) and the GUI messages proposed in
  step 17 (a lost connection now says the board was unplugged or lost power;
  a Timeout no longer blames the power supply).
- **Regression found and fixed in `UD85C08_Controller.h`:** the paragraph
  documenting that polling stops by itself after ONE `TransportError` (link
  lost, or a whole round without an answer) had been deleted by an index-
  based replacement made in step 17. Restored, with the corrected wording.
  Code and tests were not affected (109/109).

## Step 17 — The library knows the input relation (option A)

Follows from step 16: relays cannot be read safely in G3, so the library has
to know the board's mode. The board cannot be asked, hence option A: the GUI
sends the combo's relation at Connect.

- New `Controller::SetKnownInputOutputRelation()` (informs, sends nothing);
  `SendSetInputOutputRelation()` records the relation once acknowledged; the
  relation is forgotten on `Connect`.
- Polling by relation: **G0, G1, G2** - no input-edge protection, relay
  changes are reported at once; **G3** - relays are never read (their state
  comes from the commands sent; a pulse started by an input is not reported);
  **unknown** - the previous best-effort behaviour (2 s protection).
- `--monitor-inputs` in the CLI takes `--relation 0-3` (nothing is sent) and
  prints a note when it is missing.
- `FakeBoard` now understands the `G0..G3` commands and configures its input
  action accordingly (none / toggle / latch / 1.2 s pulse), and counts relay
  and input reads. 8 new self-tests (109 total): G1 reported at once, a tap
  the polling never sees leaves a G3 pulse alone, relays never read in G3
  while inputs still are, commands still publish in G3, `SetKnown...` works
  without sending, leaving G3 resumes relay reads, the relation is forgotten
  on Connect. Four mutations (G3 still reads, protection always on,
  relation not forgotten, relation not recorded) each make tests fail; the
  first one reproduces the real-board failure. Three clean runs, clean under
  ThreadSanitizer.
- GUI updated (Connect sends the combo's relation, see `GUI/README.md`).
  **Confirmed on the real board**: G3 pulses fall back every time, relays
  switched by their input show up at once in G0-G2, and a port with no
  board is refused at Connect.

## Step 16 — Momentary command pulse measured; limits of G3 acknowledged

- `--measure-pulse 1` on the board: a Momentary COMMAND pulse lasts 0.8 to
  1.0 s in AT and in HEX (not 0.5 s). The relay was left unread for 1 s
  after the command, i.e. right at the upper bound; `MomentaryQuietMs` is
  now **2000**, and the simulated Momentary lasts 1.0 s. (The self-tests do
  not discriminate 1 s from 2 s - the simulated read lands just after the
  pulse either way - so this change rests on the measurement.)
- G3 re-test with the 2 s protection: the relay still sometimes stays on and
  falls at the next press or the one after. Cause (analysis, consistent with
  the log): the protection only starts once an input change has been
  SEEN, polling samples inputs about every 0.5 s, and a short tap can fall
  between two samples. The board keeps no memory of input changes, so this
  cannot be fixed by reading faster. **There is no reliable fix as long as
  relays are read in G3**; the proposal (library told the input relation;
  no relay reads in G3; no input-edge protection in G0-G2) is written up in
  `HARDWARE_ASSUMPTIONS.md`, awaiting a decision on how the library learns
  the relation.

## Step 15 — Input-triggered pulses (G3): polling no longer cancels them

- Bench tests with `--set-relation 3` (input held grounded): input reads
  alone let the relay fall back; relay reads alone leave it stuck on. The
  board's own input-triggered pulse is cancelled by a relay read, exactly
  like a Momentary/Delay command, and it is edge-triggered
  (`DEVICE_BEHAVIOR.md`).
- **Fix (option 1 of the three considered):** each polling round now reads
  an input BEFORE its relay; when the input just changed, the relay is left
  unread for 1 s (`ExtendQuietPeriod`, which never shortens a longer
  protection such as a running Delay). The first observation of an input
  is not an edge. A blind spot of ~20 ms between the two reads remains.
- Consequences, documented in `Controller.h`: a relay switched by its
  input is reported about 1 s late, and a G3 pulse (0.5 s) is never shown.
- `FakeBoard` learned the input relations (toggle on edge, 0.5 s pulse on
  edge). 4 new self-tests (101 total); three mutations (no edge
  protection, protection overwritten instead of extended, first
  observation counted as an edge) each fail exactly one of them. Four
  clean runs and clean under ThreadSanitizer.
- Bench result: G1 behaves as designed (relay reported ~1 s after the
  press). G3 is inconclusive: after about a dozen presses the relay was
  found on once, either a G3 pulse longer than the 1 s protection or a
  press in the ~20 ms blind spot.
- Added `--probe-input-pulse <channel> [delay_ms ...]` to the CLI: for each
  delay it waits for a press (reading only the input), reads the relay once
  that long after it, closes it, and brackets the pulse duration. Checked
  against a simulated G3 board (500 ms -> "between 300 and 600", 1500 ms ->
  "between 1300 and 2000"); not yet run on the real board.
- **Measured on the board with `--probe-input-pulse 1`: the G3 pulse
  lasts between 0.9 and 1.3 s**, not 0.5 s. That explains the single
  `Relay 1 -> ON` of the first G3 series: the 1 s protection ended while the
  pulse was still running. `InputEdgeQuietMs` raised from 1000 to **2000 ms**.
  The simulated G3 pulse in the self-tests is now 1.2 s, and putting the
  constant back to 1000 makes exactly that test fail. Price: a relay
  switched by its input is reported about 2 s late.
- Added `--measure-pulse <channel> [delay_ms ...]`: same technique for a
  Momentary COMMAND pulse, with no action needed from the user. Checked
  against simulated boards (500 ms and 1000 ms); not yet run on the board.
- Open: re-test G3 with the 2 s protection, measure the Momentary command
  pulse, and whether the GUI should apply the chosen relation at Connect
  (`HARDWARE_ASSUMPTIONS.md`).

## Step 14 — Momentary/Delay vs polling: cause found and fixed

- First GUI test on the board: everything worked except Momentary and
  Delay, whose relays switched on but never fell back. The CLI reproduced
  it (AT and HEX) once the same traffic was generated, so it was not a GUI
  bug.
- Added `--probe-pulse <channel> [delay <s>]` to the CLI: replays a pulse
  followed by different kinds of traffic with exact timing and checks by
  itself whether the relay fell back. Its result on the real board (AT,
  HEX and Delay alike): **only reading the state of the pulsing relay
  cancels the pulse**; input reads and reads of other relays do not.
- **Fix in `Controller`:** after `SendMomentary` / `SendDelay`, the polling
  skips that one relay for the nominal pulse length + 1 s (everything else
  keeps being polled). The protection starts before the command is sent
  (so the polling thread cannot slip a read in), is lifted early when the
  relay is closed (`SendClose`, "all close", or closed by a Latch), and is
  dropped if the command fails. `QueryRelayStatus` is deliberately left
  unguarded - it stays the raw read the diagnostic needs - and its header
  now carries a warning.
- `FakeBoard` (self-tests) now models the hazard: real-clock Momentary and
  Delay timers, and cancellation when the pulsing relay is read. A
  mutation check (protection disabled) makes exactly the 4 relevant tests
  fail, i.e. the original defect. Self-tests: 90 -> 97, three clean runs
  and clean under ThreadSanitizer. The probe gained a 7th scenario that
  runs the library's real polling.
- **Confirmed on the real board:** scenario 7 reports `relay FELL BACK` in
  AT, in HEX and for Delay (AT only), scenarios 4 and 6 still report the
  cancelled pulse as expected, and the GUI now lets Momentary/Delay relays
  fall back.
- Still open (`HARDWARE_ASSUMPTIONS.md`): the real Momentary length behind
  the 1 s margin, and the G3 input relation, where the board pulses a relay
  on its own and the library cannot know when.

## Step 13 — Confirmed commands and live relay/input polling (Core)

Follows from the bench measurements (`DEVICE_BEHAVIOR.md`): the board never
speaks first, answers every command in ~20 ms, and relays change on their
own (Momentary/Delay fall-back, Latch, input-driven relations).

- **`Send*` now waits for the board's answer** (250 ms at most) instead of
  returning as soon as the bytes were written. A silent port is reported
  as `Timeout` rather than a false success, and replies no longer pile up
  unread in the serial buffer. Accepted cost: a click can block the caller
  up to 250 ms if nothing answers.
- **Relay states are published**: a confirmed `Send*` raises
  `RelayStateChanged` for the relay concerned (all 8 for the "all"
  commands); `SendLatch` also reports the other relays closed, which the
  board does silently.
- **Polling reads relays and inputs.** `StartInputPolling` /
  `StopInputPolling` are renamed `StartPolling` / `StopPolling` (API
  change; CLI updated). The first round reports every channel. Default
  interval 100 ms; the 10 ms pause between channels was dropped.
- **Polling also stops, with one `TransportError(Timeout)`, when a whole
  round gets no answer** (port open but nothing listening).
- `Connect` discards bytes left over from an earlier session and resets
  the known states.
- `MockSerialTransport`: now thread-safe; new `SetResponder()` lets a
  test simulate a stateful board; `Read()` really waits out its timeout.
  Self-tests gain a `FakeBoard` speaking the real AT and HEX formats.
- Self-tests: 74 -> 90, run 6 times without a flake and clean under
  ThreadSanitizer (no data race between the polling thread and callers).
- `--monitor-inputs` in the CLI now prints relay changes too.

## Step 12 — Latch verified on the real board

- Latch exclusivity confirmed in AT and HEX: latching a channel silently
  closes **all** other relays (not only the previously latched one), and
  status reads reflect it. Recorded in `DEVICE_BEHAVIOR.md`.
- Bare `--port COM12` now works on the real board (fix from step 11).
- No code change.

## Step 11 — COM10+ port names

- Bench result: `--port COM12` failed while `--port \\.\COM12` worked
  (Windows only accepts the bare `COMn` form up to COM9).
- `SerialTransport::Open` now always applies `NormalizePortName`
  (new internal header `Core/src/PortName.h`), so any consumer can pass
  either form. The GUI already built the extended form itself, which is
  why only the CLI was affected.
- 4 self-tests added (74 total).

## Step 10 — First tests on a real board

The board finally arrived; bench results are recorded in
`DEVICE_BEHAVIOR.md`. They exposed several differences from the datasheet
and from our assumptions, all fixed here:

- **AT decoder rewritten.** The board answers `OFF1` / `ON1` / `OK1` with
  no terminator and frames relay replies irregularly (`\nOpen1 \n`).
  `AtCommandCodec` previously waited for a line terminator, so
  `--read-input` always timed out. It now recognises "known prefix + one
  channel character" and ignores CR/LF/space padding around it.
- **AT commands no longer carry a CR/LF** (none is required, and none
  was verified to be harmless when queries are chained).
- **HEX decoder extended** with what the datasheet omitted: input state
  replies (`0x81` / `0x82`) and the acknowledgement of the I/O relation
  command by echoing the command frame (header `55 56`).
- **Bug fixed in `Controller::Query`:** after the codec resynchronized
  (skipped junk), the loop went back to reading instead of retrying on
  the bytes already buffered, so a valid reply behind a junk byte was
  only found after waiting out the whole timeout.
- **Polling now stops on a lost link.** Unplugging the cable used to make
  `StartInputPolling` raise the same `TransportError` for every channel
  of every round forever. It now raises one event and stops by itself.
  The CLI's `--monitor-inputs` reports "Connection lost" accordingly.
- Self-tests: 51 -> 70, adding every frame/reply captured on the real
  board (AT and HEX), byte-level resync cases, and the single-event
  polling behaviour.
- Docs: new `DEVICE_BEHAVIOR.md` (measured behaviour, now the reference);
  `HARDWARE_ASSUMPTIONS.md` reduced to what is still open, plus the
  proposed design for live LED updates (not yet implemented: polling both
  relays and inputs, and making `Send*` wait for the board's reply).

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

- The planned third project will use the **Latch** mode (when a relay is on,
  the others fall) together with **input reading**. Neither involves a
  timer on the board, so reading relays and inputs is harmless there; the
  pulse-related precautions above do not concern it. To be taken up later.

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
