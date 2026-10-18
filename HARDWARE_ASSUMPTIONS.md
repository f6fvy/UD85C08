# Hardware assumptions still to verify

The first bench tests on a physical board are done: what they established
is recorded in [`DEVICE_BEHAVIOR.md`](DEVICE_BEHAVIOR.md), and the codecs
now follow it. This file only lists what is **still open** (assumed, or
never exercised on the real device), plus a short record of what the
bench tests settled.

## Settled by the bench tests

- [x] AT command terminator: none required (we no longer send CR/LF).
- [x] AT response framing: irregular; handled by prefix recognition.
- [x] Response to `AT+Rx`, `AT+Tx`, `AT+Lx`, `AT+Mx`, `AT+Dx=`: `OpenN` / `CloseN`.
- [x] Meaning of the digit in `OKx`: the G mode that was set.
- [x] `AT+AO` / `AT+AC` reply with `OpenA` / `CloseA` (channel 0 = all).
- [x] HEX responses to Toggle / Interlock / Momentary / Read status: same
  frame format as Open / Close.
- [x] HEX Read input: command byte `0x81` (grounded) / `0x82` (floating).
- [x] HEX "set I/O relation" is acknowledged by echoing the command frame.
- [x] HEX checksum formula (also holds for every response).
- [x] No echo of sent commands; no unsolicited messages (polling required).
- [x] Timeouts: the board answers in about 17-20 ms, so the 500 ms query
  timeout and 300 ms polling timeout are very generous (kept on purpose).
- [x] `CSerialPort2::Read` with the chosen `COMMTIMEOUTS` behaves as
  intended: queries return and time out cleanly.
- [x] CH340 driver, `COM6` and `\\.\COM6` port name forms.
- [x] Why Momentary / Delay never fell back under polling: reading the state
  of the pulsing relay cancels its pulse (see `DEVICE_BEHAVIOR.md`); any
  other traffic is harmless. The polling now skips that relay while its
  pulse runs. Confirmed on the real board with `--probe-pulse 1` in AT,
  in HEX and (AT only) for Delay: the library's own polling lets the pulse
  fall back (scenario 7), while the raw relay-status read still cancels it
  (scenarios 4 and 6). The GUI also behaves correctly.
- [x] Latch (`AT+Lx` / HEX Interlock) exclusivity, in both protocols:
  latching channel 2 closes channel 1, and latching channel 1 also closes
  an unrelated channel 5 that had been opened. No message reports any of
  it, and reading the status afterwards returns the right state.
- [x] Ports above COM9: `COM12` fails, `\\.\COM12` works; the library now
  always adds the prefix (`PortName.h`), so either form can be passed.
  Confirmed on the real board with a bare `--port COM12` after the fix.
- [x] `--read-input` works on the real board in AT and HEX, with the input
  both floating and tied to ground (after the decoder fixes).
- [x] USB unplug during `--monitor-inputs`: a single "Connection lost"
  event is raised and polling stops (previously an endless error stream).

## Still open

- [x] **Length of a Momentary command pulse:** 0.8 to 1.0 s, identical in AT
  and HEX (`--measure-pulse 1`; not the 0.5 s of the datasheet). The relay
  is now left unread for 2 s after a Momentary command (Delay: requested
  time + 1 s). The self-tests cannot tell 1 s from 2 s here (in the
  simulation the read lands just after the pulse either way): the value
  rests on the measurement.

- [x] **G1 on the real board.** A relay switched by its input is reported
  after the protection delay of an unknown relation (2 s now), as designed.
  With the relation known the delay is gone (see below).

- [x] **G3 cannot be made reliable while relays are being read** (see
  `DEVICE_BEHAVIOR.md`): a tap shorter than one polling round goes unseen, and
  the next relay read cancels the pulse. Settled by not reading relays at all
  once the library knows the board is in G3.

- [x] **Decision: how the library learns the input relation: option A.** The
  GUI sends its combo's relation at every Connect, so board and combo always
  agree and the library knows the relation
  (`SendSetInputOutputRelation` records it once the board acknowledges).
  `SetKnownInputOutputRelation` informs the library without sending anything,
  for consumers that must not touch the board's mode (the CLI's
  `--monitor-inputs --relation N`). The relation is forgotten on `Connect`.

- [x] **Input relation awareness, on the real board.** Confirmed with the
  GUI: in G3 the relay falls back every time (including very short taps) and
  its LED does not show the pulse; in G0/G1/G2 a relay switched by its input
  shows up at once; Connect sends the combo's relation first and refuses a
  port where the board does not answer.

- [ ] **Momentary / Delay fall-back in HEX.** For AT it is confirmed that
  nothing is sent when the relay falls back. Not recorded for HEX.

- [ ] **Recovery after a USB unplug.** Unplugging is now handled (see the
  settled list). Still to check: after replugging, does `Disconnect()`
  followed by `Connect()` recover the port, or does Windows keep a stale
  handle (or assign a new COM number)?

- [ ] **Bursts of back-to-back queries.** Polling will issue 8 (or 16)
  queries in a row. Confirm that no answer is lost or merged with
  another at that pace.

- [ ] **Refresh latency of the polling.** One round is 16 queries (8 relays
  + 8 inputs) at ~20 ms each, i.e. roughly 0.3-0.4 s plus the pause between
  rounds. Measure how long a state change really takes to reach the LEDs
  on the real board, and tune the polling interval if needed.

- [ ] **Silent board detection.** With the USB cable plugged but the
  board's 12/24 V supply off (or the wrong COM port selected), polling
  should raise one `TransportError` with `errorCode == Timeout` after
  about 1.6 s, and stop. Only checked against a mock so far.

## GUI design - live LEDs (Core done, GUI wiring to validate)

The measurements settled the design, now implemented in `Controller`:

- `StartPolling` reads the 8 relays **and** the 8 inputs each round and
  raises `RelayStateChanged` / `InputStateChanged`. The first round reports
  every channel, so a display can initialise itself.
- Every `Send*` waits for the board's answer (250 ms at most) and publishes
  the relay state it reports (`SendLatch` also reports the other relays
  closed). A silent port is reported as `Timeout`.
- Polling stops by itself after ONE `TransportError`, when the link fails
  or when a whole round gets no answer.
- `Connect` discards bytes left over from an earlier session.

Still to validate on the real board with the GUI: the LEDs follow the
relays and inputs, the lost-link message, and the "board not answering"
message (see the open items above).
