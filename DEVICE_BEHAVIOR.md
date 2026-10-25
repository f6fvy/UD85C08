# Measured device behavior

What a physical UD85C08 board actually does, as measured on the bench
(Windows, USB serial, 9600 8N1, seen as COM6). Where this differs
from, or completes, the manufacturer's datasheet, **this file is the
reference** - the codecs in `Core/src/` are written against it.

Items still unverified are listed in `HARDWARE_ASSUMPTIONS.md`.

## General

| Topic | Measured |
|---|---|
| AT command terminator | **None required.** `AT+O1` alone is enough, no CR/LF. |
| Echo | **None.** The board never repeats what it receives. |
| Unsolicited messages | **None.** A changing input or relay is never reported spontaneously, so the host **must poll**. |
| Response latency | Measured over 1600 queries each (`--stress-test`): AT **14.6 ms** on average (12.3-17.4 ms), HEX **20.0 ms** (19.7-20.6 ms). No answer lost. |
| `G` mode persistence | **Persistent**: the mode survives a power cycle. |
| Port name | Up to COM9 both `COM6` and `\\.\COM6` open. From COM10 up only the extended form works: `COM12` fails, `\\.\COM12` works (a Windows rule, not specific to the board). `SerialTransport` always adds the prefix. |
| Driver | Port appeared on its own (CH340). |
| Power | Cutting the board's supply makes the COM port **disappear from Windows** (observed). The explanation given is that the USB serial part is handled by the board's own microcontroller rather than by a separate bus-powered chip (the datasheet mentions a CH340). Either way, a power cut looks exactly like an USB unplug to the software. |

## AT protocol

Every command is answered, but the framing is **irregular**, which is why
`AtCommandCodec` recognises "known prefix + one channel character" rather
than lines. `\n` below is a line feed; note the **trailing space** after
`Open<n>` and its absence after `Close<n>`.

| Command | Response | Notes |
|---|---|---|
| `AT+R1` | `\nClose1\n` or `\nOpen1 \n` | Same format as the Open/Close answers. |
| `AT+O1` | `\nOpen1 \n` | |
| `AT+C1` | `\nClose1\n` | |
| `AT+T1` (twice) | `\nOpen1 \n` then `\nClose1\n` | Reports the **resulting** state. |
| `AT+L1` then `AT+L2` | `\nOpen1 \n` then `\nOpen2 \n` | Only the targeted channel answers. **Every other relay is closed silently** (verified in AT and in HEX Interlock, including an unrelated relay opened beforehand); reading their status afterwards is correct. |
| `AT+M1` | `\nOpen1 \n` | **No message** when the relay falls back (about 0.5 s later). |
| `AT+D1=0005` | `\nOpen1 \n` | **No message** when the delay expires. |
| `AT+AO` / `AT+AC` | `\nOpenA \n` / `\nCloseA\n` | `A` = all channels. |
| `AT+I1` | `OFF1` (input floating) / `ON1` (input tied to ground) | **No terminator at all.** |
| `AT+G1` / `AT+G0` | `OK1` / `OK0` | **No terminator at all.** The digit is the G mode that was set. |

## HEX protocol

Frames are 8 bytes; the checksum is the sum of the first 7 bytes modulo
256 (also true of every response below).

| Command | Frame sent | Frame received |
|---|---|---|
| Open ch1 | `55 56 00 00 00 01 01 AD` | `33 3C 00 00 00 01 01 71` |
| Close ch1 | `55 56 00 00 00 01 02 AE` | `33 3C 00 00 00 01 02 72` |
| Read status ch1 | `55 56 00 00 00 01 00 AC` | `33 3C 00 00 00 01 02 72` (closed) or `33 3C 00 00 00 01 01 71` (open) |
| Toggle ch1 | `55 56 00 00 00 01 03 AF` | `33 3C 00 00 00 01 01 71` (state after the command) |
| Interlock ch1 | `55 56 00 00 00 01 04 B0` | `33 3C 00 00 00 01 01 71` |
| Momentary ch1 | `55 56 00 00 00 01 05 B1` | `33 3C 00 00 00 01 01 71` (only reply: no echo, and no frame when the relay falls back) |
| Read input ch1 | `55 56 00 00 00 01 09 B5` | `33 3C 00 00 00 01 82 F2` (floating) / `33 3C 00 00 00 01 81 F1` (tied to ground) |
| All open | `55 56 00 00 00 00 07 B2` | `33 3C 00 00 00 00 01 70` |
| All close | `55 56 00 00 00 00 08 B3` | `33 3C 00 00 00 00 02 71` |
| Set I/O relation G0 | `55 56 00 00 00 00 0A B5` | same frame echoed back |
| Set I/O relation G1 | `55 56 00 00 00 01 0A B6` | same frame echoed back |

So, in a `33 3C` response, the command byte is `01` (relay open), `02`
(relay closed), `81` (input on) or `82` (input off) - the last two are
not in the datasheet.

## Reading a relay's state cancels its Momentary / Delay pulse

The Momentary (~0.5 s) and Delay timers are run by the board's firmware.
**Asking the board for the state of a relay while that relay's pulse is
running cancels the pulse**: the relay then stays on until something else
changes it. Nothing else does this.

Measured with `UD85C08_CLI --probe-pulse 1` (pulse, then one kind of
traffic 100 ms later, then a check 3 s on), identical in AT, in HEX, and
for Delay (3 s):

| Traffic sent during the pulse | Relay |
|---|---|
| none | falls back |
| one input read, same channel | falls back |
| one input read, another channel | falls back |
| **one relay-status read, same channel** | **stays on (pulse cancelled)** |
| one relay-status read, another channel | falls back |
| burst of 16 reads (relays + inputs) | stays on (it contains the above) |

Earlier hand-made tests agree: one input read alone is harmless, a polling
burst is not. This is why the GUI's relays never fell back at first.

Consequence: the library leaves a relay unread while its own pulse is
running (about 1 s after Momentary, the delay + 1 s after Delay, and 2 s
after an input change); see
`Controller::StartPolling`. A direct `QueryRelayStatus` on a pulsing relay
is still dangerous and documented as such.

### Same thing with the input relation G3 ("momentary")

With `--set-relation 3`, grounding an input makes the board pulse the
matching relay by itself. Measured (AT, one `UD85C08_CLI` process per read,
input 1 grounded and **held** during the loop):

| Traffic during the pulse | Relay |
|---|---|
| input reads only (`--read-input 1`) | falls back |
| relay reads only (`--read-relay 1`) | **stays on (pulse cancelled)** |

So a pulse started by an input is cancelled by a read of that relay exactly
like a Momentary/Delay command, and the library cannot tell when the board
starts it. Also: the pulse fell back **while the input was still grounded**,
so it is triggered by the edge, not by the level.

**Duration, measured with `--probe-input-pulse 1`** (relay read once at
300, 600, 900, 1300, 2000 and 4000 ms after a press): still on at 900 ms,
already off at 1300 ms, so the pulse lasts **between 0.9 and 1.3 s** - not
the 0.5 s of the datasheet. (With the full polling the symptom was
`Relay 1 -> ON` printed before `Input 1 -> On`: the relay read happened
first, inside the pulse.)

**A Momentary *command* pulse is about as long**: `--measure-pulse 1` found
the relay still on 800 ms after the command and already off at 1000 ms, in
AT and in HEX alike (so 0.8 to 1.0 s).

**Why G3 cannot be protected reliably while relays are read.** The board
does not remember input changes, and the polling samples an input only about
every half second. A tap shorter than that can be missed entirely; the relay
is then read inside its 0.9-1.3 s pulse, which cancels it. Observed: the
relay stays on, then falls at the next press or the one after.

## Consequences for the software

- **Polling is mandatory**: the board never speaks first.
- A relay can change state **without any host command**: Momentary and Delay
  fall back silently, Latch closes the other channels silently, and in the
  `G1`..`G3` input/output relations the physical inputs act on the relays
  directly. So relay states are polled - **except in G3**, where the board
  pulses a relay by itself at unpredictable moments and a relay read would
  cancel the pulse: there the relays are never read.
- **Reading a relay's state is not free**: it cancels a running Momentary,
  Delay or G3 pulse. A relay is therefore left unread while its own pulse
  runs (Momentary 0.8-1.0 s, Delay = the requested time, G3 0.9-1.3 s, plus
  margins). Reading inputs, or other relays, is harmless.
- The library has to know the input relation (the board cannot be asked, and
  remembers it across power cycles): the GUI sends it at every Connect.
- Every command is answered within about 20 ms, so a command that gets no
  answer at all is a meaningful signal (nothing, or the wrong device, is
  on that port).
