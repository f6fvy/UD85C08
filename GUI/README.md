# UD85C08_GUI

MFC dialog-based application driving a UD85C08 board through
`UD85C08::Controller` (see `Core/include/UD85C08_Controller.h`).

## ⚠️ Resource IDs must stay contiguous

`UD85C08_GUIDlg.cpp` relies on several groups of 8 resource IDs being
**consecutive, in channel order** (channel 1 = lowest ID, channel 8 =
highest, no gap):

- `IDC_LED_INPUT_1` .. `IDC_LED_INPUT_8`
- `IDC_LED_RELAY_1` .. `IDC_LED_RELAY_8`
- `IDC_BUTTON_RELAY_ON_1` .. `IDC_BUTTON_RELAY_ON_8`
- `IDC_BUTTON_RELAY_OFF_1` .. `IDC_BUTTON_RELAY_OFF_8`

This is what lets the code address a given channel's control with plain
arithmetic (`IDC_BUTTON_RELAY_ON_1 + i`) instead of a lookup table, and
is required by the `ON_CONTROL_RANGE` message-map entries for the
per-channel buttons.

**If you ever add, remove, or reorder a control in the resource editor**
that touches one of these groups, open `resource.h` afterwards and check
that the affected range is still a clean, gapless sequence of 8 IDs. The
resource editor does not warn about this - a broken range fails silently
(wrong channel's control gets manipulated, or a channel's clicks are
simply not routed to any handler), not with a compiler or runtime error.

## Getting started

Built via Visual Studio 2022's "MFC App" wizard (Dialog based, MFC in a
shared DLL). See the root `README.md` for the overall solution setup.
