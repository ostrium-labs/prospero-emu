# Key mapping

Default controls for `kyty_emulator`. Bindings can be changed in the launcher
under **Settings → Input Mapping**; this page documents the defaults.

## Keyboard

| PS5 control | Key |
|---|---|
| Cross | `J` |
| Circle | `L` |
| Square | `K` |
| Triangle | `I` |
| D-pad | Arrow keys |
| L1 | `Q` |
| R1 | `E` |
| L2 | `Z` |
| R2 | `C` |
| L3 (left stick click) | Left Shift |
| R3 (right stick click) | Left Ctrl |
| Options | Enter |
| Touch pad, left half | Backspace |
| Touch pad, right half | Tab |
| Left stick | `W` `A` `S` `D` |
| Right stick | `T` `F` `G` `H` |

The stick keys are directional: `W`/`S` and `T`/`G` are up/down, `A`/`D` and
`F`/`H` are left/right. The bindings are SDL keycodes, so on a non-QWERTY
layout the labelled keys may differ; rebind them in the launcher if needed.

## Mouse

| Action | Default |
|---|---|
| Right stick | `F7` toggles mouse-to-stick; mouse motion moves the right stick |
| Touch pad drawing | Hold left mouse button and drag while touch-pad mode is on |
| Extra buttons | `Mouse:Left`, `Mouse:Middle`, `Mouse:Right`, `Mouse:X1`, `Mouse:X2` can be bound in the launcher |

## Emulator shortcuts

| Action | Key |
|---|---|
| Toggle touch-pad mode | `F6` |
| Toggle mouse-to-right-stick | `F7` |
| Cycle keyboard player | `F8` |
| Toggle fullscreen | `F11`, or `Alt+Enter` |
| RenderDoc capture | `F1` (only when built with RenderDoc) |
| Cycle speaker volume | `1` |
| Cycle vibration intensity | `2` |
| Cycle trigger-effect intensity | `3` |

## Gamepad

Standard SDL gamepad mapping, using a DualSense/DualShock layout:

| PS5 control | SDL button |
|---|---|
| Cross | South (`A` on Xbox pads) |
| Circle | East (`B`) |
| Square | West (`X`) |
| Triangle | North (`Y`) |
| D-pad | D-pad |
| L1 / R1 | Left / right shoulder |
| L2 / R2 | Left / right trigger |
| L3 / R3 | Left / right stick click |
| Options | Start |
| Touch pad click | Touchpad button |
| Gyro emulation toggle | Back / Share |

## Keyboard player

By default the keyboard is shared with player 1, so the keyboard and a gamepad
control the same pad. To give the keyboard its own player:

```bash
./_Build/linux-no-qt/kyty_emulator --game <dir|elf|zar> --keyboard-player 2
```

`0` shares player 1 (the default); `1`–`4` select a dedicated player. `F8`
cycles the keyboard player at runtime.
