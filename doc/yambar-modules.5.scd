yambar-modules(5)

# NAME
yambar-modules - configuration file

# DESCRIPTION

Modules are what monitors your system and provides data for the status
bar.

All modules expose their data through *tags*. Each tag has a *name*,
*type* and *value*. The name and type is fixed, while the value
typically changes over time. See *yambar-tags(5)*.

The tags are rendered by _particles_. Each particle has its own way of
representing tag values. The simplest one is the _string_ particle,
which renders a text representation of the tag value. See
*yambar-particles*(5).

Note that all the examples showed below have been kept simple. Here
are a couple of tips that will improve the looks:

## Use list particles to change font and/or color

The _string_ particle, for example, cannot change font or colors in
the middle of its string. To do this, you need to wrap multiple
_string_ particles in a _list_ particle.

This is especially important if you want to use an icon font since
yambar does not do font fallback handling.

Also remember there is a short version for lists (see
*yambar-particles*(5))

For example, to render _backlight_ as " 20%", you could use:

```
content:
  - string:
      font: Font Awesome 5 Free:style=solid:pixelsize=14
      text: 
  - string:
      font: Adobe Helvetica:pixelsize=12
      text: "{percent}%"
```

## Use map particles to handle 'state'

Several modules have a _state_ tag that can be used to render
different particles depending on the module's state.

For example, you might want different things to be shown for a
_network_ interface that is *down* or *up*. You could further
differentiate between an *up* interface that has or has not an IP
address assigned to it.

Below is an example, where a wired connection is not renderer at all
when disconnected.

When connected, it is rendered in the default text color if it is up
and also has an IPv4 address. If it is up, but does not have an IPv4
address, it is rendered in a semi-transparent white color.

Finally, if it is down, or in any other unknown state, it is rendered
in red.

```
content:
  map:
    tag: carrier
    values:
      false: {empty: {}}
      true:
        map:
          tag: state
          default: {string: {text: , font: *awesome, foreground: ffffff66}}
          values:
            up:
              map:
                tag: ipv4
                default: {string: {text: , font: *awesome}}
                values:
                  "": {string: {text: , font: *awesome, foreground: ffffff66}}
```

## Use yaml anchors

You often end up using the same definitions in a lot of places. This
is particular true for fonts. But it can also be true when mapping
state.

In these cases, you can define an anchor point, either at top-level,
or in a module's _anchors_ attribute:

```
awesome: &awesome Font Awesome 5 Free:style=solid:pixelsize=14

```

Then reference it in your particle definitions:

```
content:
  string: {text: , font: *awesome}
```

# GENERIC CONFIGURATION

Each module defines its own configuration format. However, the
following attributes are supported by all modules:

[[ *Name*
:[ *Type*
:[ *Description*
|  content
:  particle
:  A particle describing how the module's information is to be rendered.
   See *yambar-particles*(5)
|  anchors
:  associative array
:  Free-to-use associative array, where you can put yaml anchor definitions
|  font
:  font
:  no
:  Font to use in the content particle. This is an inherited attribute.
|  foreground
:  color
:  no
:  Foreground (text) color of the content particle. This is an
   inherited attribute.

# ALSA

Monitors an alsa soundcard for volume and mute/unmute changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  volume
:  range
:  Volume level, with min and max as start and end range values
|  muted
:  bool
:  True if muted, otherwise false


## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  card
:  string
:  yes
:  The soundcard name. _Default_ might work.
|  mixer
:  string
:  yes
:  Mixer channel to monitor. _Master_ might work.

## EXAMPLES

```
bar:
  left:
    - alsa:
        card: hw:PCH
        mixer: Master
        content: {string: {text: "{volume}"}}
```

# BACKLIGHT

This module reads monitor backlight status from
_/sys/class/backlight_, and uses *udev* to monitor for changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  brightness
:  range
:  The current brightness level, in absolute value
|  percent
:  range
:  The current brightness level, in percent

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  yes
:  The backlight device's name (one of the names in */sys/class/backlight*)

## EXAMPLES

```
bar:
  left:
    - backlight:
        name: intel_backlight
        content:
          string: {text: "backlight: {percent}%"}
```

# BATTERY

This module reads battery status from _/sys/class/power_supply_ and
uses *udev* to monitor for changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Battery device name
|  manufacturer
:  string
:  Name of the battery manufacturer
|  model
:  string
:  Battery model name
|  state
:  string
:  One of *full*, *charging*, *discharging* or *unknown*
|  capacity
:  range
:  capacity left, in percent
|  estimate
:  string
:  Estimated time left (to empty while discharging, or to full while
   charging), formatted as HH:MM.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  yes
:  Battery device name (one of the names in */sys/class/power_supply*)
|  poll-interval
:  int
:  no
:  How often, in seconds, to poll for capacity changes (default=*60*)

## EXAMPLES

```
bar:
  left:
    - battery:
        name: BAT0
        poll-interval: 30
        content:
          string: {text: "BAT: {capacity}% {estimate}"}
```

# CLOCK

This module provides the current date and time.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  time
:  string
:  Current time, formatted using the _time-format_ attribute
|  date
:  string
:  Current date, formatted using the _date-format_ attribute

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  time-format
:  string
:  no
:  *strftime* formatter for the _time_ tag (default=*%H:%M*)
|  date-format
:  string
:  no
:  *strftime* formatter for the _date_ date (default=*%x*)

## EXAMPLES

```
bar:
  left:
    - clock:
        time-format: "%H:%M %Z"
        content:
          string: {text: "{date} {time}"}
```

# I3 (and Sway)

This module monitors i3 and sway workspaces.

Unlike other modules where the _content_ attribute is just a single
*particle*, the i3 module's _content_ is an associative array mapping
i3/sway workspace names to a particle.

You can specify a default particle to use with an empty workspace
name, *""*.

It also recognizes the special name *current*, which always represents
the currently focused workspace.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  The workspace name
|  visible
:  bool
:  True if the workspace is currently visible (on any output)
|  focused
:  bool
:  True if the workspace is currently focused
|  urgent
:  bool
:  True if the workspace has the urgent flag set
|  state
:  string
:  One of *urgent*, *focused*, *unfocused* or *invisible* (note:
   *unfocused* is when it is visible, but neither focused nor urgent).
|  application
:  string
:  Name of application currently focused on this workspace
|  title
:  string
:  This workspace's focused window's title

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  content
:  associative array
:  yes
:  Unlike other modules, _content_ is an associative array mapping
   workspace names to particles. Use *""* to specify a default
   fallback particle, or *current* for the currently active workspace.
|  left-spacing
:  int
:  no
:  Space, in pixels, on the left-side of each rendered workspace particle
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right-side of each rendered workspace particle
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

## EXAMPLES

This renders all workspace names, with an *\** indicating the
currently focused one. It also renders the currently focused
application name and window title.

```
bar:
  left:
    - i3:
        content:
          "":
            map:
              tag: state
              default: {string: {text: "{name}"}}
              values:
                focused: {string: {text: "{name}*"}}
          current: { string: {text: "{application}: {title}"}}
```

# LABEL

This module renders the provided _content_ particle, but provides no
additional data.

## TAGS

None

## CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION*)

## EXAMPLES

```
bar:
  left:
    - label:
        content: {string: {text: hello world}}
```

# MPD

This module provides MPD status such as currently playing
artist/album/song.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  state
:  string
:  One of *offline*, *stopped*, *paused* or *playing*
|  repeat
:  bool
:  True if the *repeat* flag is set
|  random
:  bool
:  True if the *random* flag is set
|  consume
:  bool
:  True if the *consume* flag is set
|  album
:  string
:  Currently playing album (also valid in *paused* state)
|  artist
:  string
:  Artist of currently playing song (also valid in *paused* state)
|  title
:  string
:  Title of currently playing song (also valid in *paused* state)
|  pos
:  string
:  *%M:%S*-formatted string describing the song's current position
   (also see _elapsed_)
|  end
:  string
:  *%M:%S*-formatted string describing the song's total length (also
   see _duration_)
|  elapsed
:  realtime
:  Position in currently playing song, in milliseconds. Can be used
   with a _progress-bar_ particle.
|  duration
:  int
:  Length of currently playing song, in milliseconds

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  host
:  string
:  yes
:  Hostname/IP/unix-socket to connect to
|  port
:  int
:  no
:  TCP port to connect to

## EXAMPLES

```
bar:
  left:
    - mpd:
        host: /run/mpd/socket
        content:
          string: {text: "{artist} - {album} - {title} ({end})"}
```

# NETWORK

This module monitors network connection state; disconnected/connected
state and MAC/IP addresses.

Note: while the module internally tracks all assigned IPv4/IPv6
addresses, it currently exposes only a single IPv4 and a single IPv6
address.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Network interface name
|  index
:  int
:  Network interface index
|  carrier
:  bool
:  True if the interface has CARRIER. That is, if it is physically connected.
|  state
:  string
:  One of *unknown*, *not present*, *down*, *lower layers down*,
   *testing*, *dormant* or *up*. You are probably interrested in *down* and *up*.
|  mac
:  string
:  MAC address
|  ipv4
:  string
:  IPv4 address assigned to the interface, or *""* if none
|  ipv6
:  string
:  IPv6 address assigned to the interface, or *""* if none

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  Name of network interface to monitor

## EXAMPLES

```
bar:
  left:
    - network:
        name: wlp3s0
        content:
          string: {text: "{name}: {state} ({ipv4})"}
```

# REMOVABLES

This module detects removable drives (USB sticks, CD-ROMs) and
instantiates the provided _content_ particle for each detected drive.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  vendor
:  string
:  Name of the drive vendor
|  model
:  string
:  Drive model name
|  optical
:  bool
:  True if the drive is an optical drive (CD-ROM, DVD-ROM etc)
|  device
:  string
:  Volume device name (typically */dev/sd?*)
|  size
:  range
:  The volume's size, in bytes. The tag's maximum value is set to the
   underlying block device's size
|  label
:  string
:  The volume's label, or its size if it has no label
|  mounted
:  bool
:  True if the volume is mounted
|  mount_point
:  string
:  Path where the volume is mounted, or *""* if it is not mounted

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  left-spacing
:  int
:  no
:  Space, in pixels, in the left side of each rendered volume
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right side of each rendered volume
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

## EXAMPLES

```
bar:
  right:
    - removables:
        content:
          map:
            tag: mounted
            values:
              false:
                string:
                  on-click: udisksctl mount -b {device}
                  text: "{label}"
              true:
                string:
                  on-click: udisksctl unmount -b {device}
                  text: "{label}"
                  deco: {underline: {size: 2, color: ffffffff}}
```

# SWAY-XKB

This module uses *Sway* extenions to the I3 IPC API to monitor input
devices' active XKB layout. As such, it requires Sway to be running.

*Note* that the _content_ configuration option is a *template*;
*sway-xkb* will instantiate a particle list, where each item is
instantiated from this template, and represents an input device.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  id
:  string
:  Input device indentifier
|  layout
:  string
:  The input device's currently active XKB layout

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  identifiers
:  list of strings
:  yes
:  Identifiers of input devices to monitor. Use _swaymsg -t get_inputs_ to see available devices.
|  content
:  particle
:  yes
:  A particle template; each existing input device will be instantiated with this template.
|  left-spacing
:  int
:  no
:  Space, in pixels, in the left side of each rendered input device
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right side of each rendered input device
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

## EXAMPLES

```
bar:
  left:
    - sway-xkb:
        identifiers:
          - 1523:7:HID_05f3:0007
          - 7247:2:USB_USB_Keykoard
        spacing: 5
        content: {string: {text: "{id}: {layout}"}}
```

# XKB

This module monitors the currently active XKB keyboard layout and
lock-key states.

Note: this module is X11 only. It does not work in Wayland.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Name of currently selected layout, long version (e.g. "English (US)")
|  symbol
:  string
:  Name of currently selected layout, short version (e.g. "us")
|  caps_lock
:  bool
:  True if *CapsLock* is enabled
|  num_lock
:  bool
:  True if *NumLock* is enabled
|  scroll_lock
:  bool
:  True if *ScrollLock* is enabled

## CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION*)

## EXAMPLES

```
bar:
  left:
    - xkb:
        content:
          string: {text: "{symbol}"}
```

# XWINDOW

This module provides the application name and window title of the
currently focused window.

Note: this module is X11 only. It does not work in Wayland. If you are
running Sway, take a look at the _i3_ module and its _current_ tag.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  application
:  string
:  Name of the application that owns the currently focused window
|  title
:  string
:  The title of the currently focused window

## CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION*)

## EXAMPLES

```
bar:
  left:
    - xwindow:
        content:
          string: {text: "{application}: {title}"}
```

# SEE ALSO

*yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)