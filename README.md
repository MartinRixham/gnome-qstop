# qstop

GNOME's quick settings menu as a terminal user interface, written in the style of
[btop++](https://github.com/aristocratos/btop): C++23, no dependencies beyond the standard
library, rounded boxes with superscript numbered titles, gradient meters, btop theme files and
btop's keyboard/mouse conventions.

```
╭─┐¹status┌───────────────────────┐14:32:05┌────────────────────────────────────╮
│ Battery ■■■■■■■■■■  87% Discharging 5:12 left  Screenshot  Settings  Lock  Power › │
╰───────────────────────────────────────────────────────────────────────────────╯
╭─┐²sliders┌─────────────────────────────────────────────────────────────────────╮
│ Volume      ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■  82% › │
│ Microphone  ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ muted   │
│ Brightness  ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ 100%   │
╰───────────────────────────────────────────────────────────────────────────────╯
╭─┐³toggles┌─────────────────────────────────────────────────────────────────────╮
│╭──────────────────╮╭──────────────────╮╭──────────────────╮╭──────────────────╮│
││ Wi-Fi          › ││ Bluetooth      › ││ Power Mode     › ││ Night Light      ││
││ HomeNetwork      ││ On               ││ Balanced         ││ On               ││
│╰──────────────────╯╰──────────────────╯╰──────────────────╯╰──────────────────╯│
╰─┘↵ toggle└─┘e expand└─┘h help└─┘q quit└──────────────────────────────────────────╯
```

## Features

| GNOME quick settings        | qstop                                              | Backend                                  |
|-----------------------------|----------------------------------------------------|------------------------------------------|
| Battery indicator           | Status box meter, state and time remaining         | `/sys/class/power_supply`                |
| Screenshot button           | `s`                                                | xdg-desktop-portal (`gdbus`)             |
| Settings button             | `t`, submenus link to their Settings panel         | `gnome-control-center`                   |
| Lock button                 | `l`                                                | `org.gnome.ScreenSaver`, `loginctl`      |
| Power Off / Log Out menu    | `p`: Suspend, Restart, Power Off, Log Out          | `systemctl`, `gnome-session-quit`        |
| Volume slider + output menu | Mute, adjust, choose output device                 | `pactl` (PipeWire / PulseAudio)          |
| Microphone slider + menu    | Mute, adjust, choose input device                  | `pactl`                                  |
| Brightness slider           | Screen backlight                                   | logind `SetBrightness` or `brightnessctl`|
| Keyboard backlight          | Keyboard backlight slider                          | logind `SetBrightness` or `brightnessctl`|
| Wired                       | Connect / disconnect                               | `nmcli`                                  |
| Wi-Fi + network list        | Toggle radio, scan, connect (with password), disconnect | `nmcli`                             |
| Bluetooth + device list     | Toggle power, connect / disconnect paired devices  | `bluetoothctl`, `rfkill`                 |
| Power Mode                  | Performance / Balanced / Power Saver               | power-profiles-daemon (`busctl`)         |
| Night Light                 | Toggle                                             | `gsettings`                              |
| Dark Style                  | Toggle                                             | `gsettings`                              |
| VPN                         | Toggle / choose connection                         | `nmcli`                                  |
| Airplane Mode               | Toggle                                             | `rfkill`, `/sys/class/rfkill`            |
| Do Not Disturb              | Toggle                                             | `gsettings`                              |

Toggles only appear when their backend is available. All commands are run directly with `posix_spawn`,
never through a shell, on background workers so the interface never blocks. Failures are shown in the
bottom border of the status box.

## Keys

| Key                 | Action                                                   |
|---------------------|----------------------------------------------------------|
| `q`, `Ctrl-C`       | Quit                                                     |
| `h`, `?`, `F1`      | Help                                                     |
| `1` `2` `3`         | Toggle status, sliders and toggles boxes                 |
| Arrows, `Tab`       | Move focus                                               |
| `Enter`, `Space`    | Activate toggle or button, mute focused slider           |
| `e`                 | Open the submenu (`›`) of the focused element            |
| `←` `→`, `+` `-`    | Adjust focused slider                                    |
| `m` / `M`           | Mute output / microphone                                 |
| `w` `i` `b` `r` `n` `d` `v` `a` `o` | Wi-Fi, Wired, Bluetooth, Power Mode, Night Light, Dark Style, VPN, Airplane Mode, Do Not Disturb |
| `W` / `B`           | Show Wi-Fi networks / Bluetooth devices                  |
| `s` `t` `l` `p`     | Screenshot, Settings, Lock, Power menu                   |

Highlighted letters in labels show the hotkey, as in btop. With `vim_keys = true`, `h j k l` move focus
and help and lock move to `H` and `L`. Mouse: click to activate, click `›` for submenus, click or drag
on sliders to set them and scroll over sliders to adjust.

## Building

Requires GCC 14+ or Clang 19+ (C++23) and GNU Make.

```sh
make
sudo make install          # PREFIX=/usr/local by default
```

## Configuration

The config file is written to `$XDG_CONFIG_HOME/qstop/qstop.conf` on first run, with a description
of every option. Options include `color_theme`, `theme_background`, `truecolor`, `force_tty`,
`vim_keys`, `rounded_corners`, `disable_mouse`, `shown_boxes`, `update_ms`, `clock_format`,
`volume_step`, `brightness_step`, `allow_volume_above_100`, `confirm_power_actions` and
`show_battery_time`.

## Themes

qstop reads btop formatted `.theme` files from `../share/qstop/themes` relative to the binary,
`$XDG_CONFIG_HOME/qstop/themes` and **every btop theme directory**, so installed btop themes work as
they are. btop color names map onto qstop elements (`cpu_box` → status box, `cpu_*` → volume
gradient and so on), while qstop specific names (`status_box`, `volume_start`, `battery_end`, ...)
take precedence. `adwaita` and `adwaita-dark` themes following the GNOME palette are included.

Wi-Fi passwords are passed to `nmcli` as an argument and are therefore briefly visible to other users
of the machine in the process list, as with any `nmcli device wifi connect ... password` invocation.

## License

GNU General Public License v3.0 or later; see [LICENSE](LICENSE). Portions of the terminal, theme,
drawing and input code are adapted from btop++, Copyright 2021 Aristocratos, which is licensed under
the Apache License 2.0.
