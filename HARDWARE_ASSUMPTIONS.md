# Hardware assumptions to verify

This file centralizes every point in the code that currently relies on an
**assumption** (deduced from the datasheet, or a "reasonable" guess),
since no physical UD85C08 board was available during development. Each
point links back to the corresponding source file/comment.

**To do as soon as the board is available**: go through this list in
order, check items off as you go, and fix the code (and its comment) if
reality differs from the assumption.

---

## AT protocol (ASCII)

- [ ] **Line terminator for sent commands.** Currently `\r\n` is appended
  after every command (`AT+O3\r\n`...). The datasheet does not specify the
  terminator expected by the firmware.
  → `Core/src/AtCommandCodec.cpp`, `BuildSimple` / `EncodeDelay`.

- [ ] **Line terminator for received responses.** The decoder accepts
  `\r`, `\n`, or `\r\n` as a line ending, to be safe. Confirm/simplify once
  the real behaviour is known.
  → `Core/src/AtCommandCodec.cpp`, `TryDecode`.

- [ ] **Response to `AT+Rx` (read status).** Assumed to have the same
  format as the spontaneous response to `AT+Ox`/`AT+Cx`, i.e.
  `OpenX`/`CloseX`. The datasheet gives no explicit example for `Rx`.

- [ ] **Response (or lack thereof) to `AT+Tx`, `AT+Lx`, `AT+Mx`
  (Toggle/Latch/Momentary).** The current `Controller` sends these as
  "fire-and-forget" (`SendToggle`, `SendLatch`, `SendMomentary`) without
  waiting for/checking a response, precisely because the format is not
  guaranteed. If the firmware does answer with `OpenX`/`CloseX` like
  Open/Close, these could be turned into confirmed requests (like
  `QueryRelayStatus`).
  → `Core/src/UD85C08_Controller.cpp`, `SendToggle`/`SendLatch`/`SendMomentary`.

- [ ] **Exact meaning of the digit in the `OKx` response to `AT+Gx`.**
  Assumed to be the relation that was applied (0-3); to confirm.

- [ ] **Response format for `AT+AO` / `AT+AC` (open/close all).** The
  datasheet mentions `OpenA`/`CloseA` in its list of possible responses
  (X = 1..8/A), so `channel == 0` is treated as "all channels" in
  `DecodedResponse`, but this has not been verified.

## HEX protocol (8-byte frames)

- [x] **Checksum formula.** Verified programmatically against EVERY
  worked example in the datasheet
  (`checksum = sum of the first 7 bytes mod 256`). This point is solid,
  not a mere assumption.

- [ ] **Response format for Toggle (0x03), Interlock (0x04), Momentary
  (0x05), Read status (0x00), Read input (0x09), Set I/O relation
  (0x0A).** The datasheet ONLY documents the responses to Open (0x01) and
  Close (0x02) in its "2 return command" section. `HexFrameCodec::TryDecode`
  only recognizes those two cases; everything else currently falls back to
  `ResponseKind::Unrecognized`.
  → `Core/src/HexFrameCodec.h` (see the header comment) and
  `Core/src/HexFrameCodec.cpp`, `TryDecode`.

- [ ] **`Controller::QueryRelayStatus` in HEX mode for a "Read status"
  (0x00) command.** Since the response to Read status is not documented,
  this query is likely to systematically time out in practice until the
  real response format is added to the decoder.

- [ ] **`Controller::QueryInputStatus` in HEX mode.** Same remark: the
  response to "Read input" (0x09) is not documented.

## Transport / serial link

- [ ] **Real behaviour of `CSerialPort2::Read` with the `COMMTIMEOUTS`
  used.** Configuration chosen: `ReadIntervalTimeout = MAXDWORD`,
  `ReadTotalTimeoutMultiplier = 0`, `ReadTotalTimeoutConstant = timeoutMs`.
  Per the Win32 documentation this should give "return immediately if
  data is already available, otherwise wait up to timeoutMs" — never
  observed on a real port.
  → `Core/src/SerialTransport.cpp`, `Read`.

- [ ] **Port name format.** `Controller::Connect` (via
  `CUD85C08_GUIDlg::BuildPortPath`) now always builds the `\\.\COMx`
  extended form, for every port number (not just above COM9, as
  originally assumed here). Never verified end-to-end against a real
  device on either a low-numbered (COM1-9) or a high-numbered (COM10+)
  port - confirm both work.

- [ ] **CH340 driver.** Assumed to be natively present on recent
  Windows 10/11 (confirmed via a web search at design time), but not
  verified on the actual development machine.

- [ ] **Echo of the sent command.** Some serial devices echo the received
  command back before their own response. Nothing in the current code
  filters a potential echo — if the firmware produces one, the AT/HEX
  decoder could misinterpret the first bytes received. Worth watching
  during the first real test (inspect the raw received bytes, e.g. via
  RealTerm run in parallel).

## Default values / tuning

- [ ] **Default timeouts** (`QueryRelayStatus`/`QueryInputStatus` = 500 ms,
  internal polling = 300 ms). Arbitrary "reasonable" values; to be tuned
  once the board's real latency is known (likely very fast over local
  USB-serial, so these values are deliberately generous).

- [ ] **Default polling interval** (`StartInputPolling`, 200 ms). Same
  remark, to be tuned based on desired responsiveness vs. load on the
  link.

---

## GUI design questions (open, deliberately deferred)

Raised while designing the relay/input LED refresh mechanism (paused -
see README.md "Next step"); noted here so they are not lost before
resuming that work.

- [ ] **How should the relay LEDs reflect state after a `Send*` command?**
  Three options were considered: (a) optimistic - flip the LED as soon as
  the write succeeds, without confirmation; (b) confirmed - follow up
  with a status query before updating the LED; (c) optimistic + periodic
  correction poll. (b) and (c) are blocked on the undocumented HEX
  response formats above. Leaning towards (a) for a first version, with
  (c) added later once those formats are confirmed.

- [ ] **Momentary/Delay modes leave the LED "stuck ON".** With an
  optimistic update, clicking a Momentary or Delay button would turn the
  relay LED on, but the firmware auto-closes the relay on its own
  (after ~500 ms for Momentary, or the configured delay) without
  notifying the host - so the LED would stay lit after the relay has
  actually closed, unless a periodic confirmation poll (see above) is
  in place. Not solved by an application-side timer mimicking the
  firmware's timing, as that would be guessing at behaviour that should
  instead be observed on the real device.

## Suggested first tests once the board arrives

1. Open a serial terminal (RealTerm/Termite/PuTTY) at 9600,N,8,1 on the
   board's COM port, send `AT+O1` by hand, observe the raw bytes received
   (terminator, exact content) → addresses the AT points above.
2. Do the same with a raw HEX frame (e.g. Open channel 1:
   `55 56 00 00 00 01 01 AD`) and observe the raw response → addresses the
   HEX points above.
3. Once these two points are confirmed, run
   `UD85C08_CLI.exe --port COMx --open 1` (HEX) then
   `--protocol at --port COMx --open 1` (AT) and compare against the
   expected behaviour.
4. Test `--read-relay` / `--read-input` last, once the response formats
   have been confirmed in steps 1/2.
