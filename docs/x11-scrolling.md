# X11 wheel baseline handling

The Linux uinput mouse retains high-resolution wheel reports and accumulated
legacy detents. Absolute positioning continues through XTEST because the mixed
REL/ABS device is classified as relative by Xorg/libinput; uinput retains desktop
button delivery. Neither desktop mapping nor drag acceleration changes.

GTK and other XI2 consumers track cumulative scroll valuators. Switching from the
XTEST pointer to the uinput pointer can invalidate that baseline: the first real
wheel report establishes the baseline and has zero observable delta. Avoiding
redundant client positioning helps consecutive notches but not movement followed
by scrolling. The upstream Qt mitigation is [plank-client c0fdc2de](https://github.com/instinctual/plank-client/commit/c0fdc2deaa7b67a487c3317678db617078948eed).
Apollo instead delegates Linux motion/buttons/wheel to Inputtino, which selects
separate relative and absolute uinput devices; it is not a drop-in fix for this
backend. See [Inputtino mouse implementation](https://github.com/games-on-whales/inputtino/blob/stable/src/uinput/mouse.cpp)
and [GTK baseline discussion](https://bugzilla.gnome.org/show_bug.cgi?id=750994).

Before a nonzero wheel report, send zero **relative** scroll valuators to the
owned XInput device. Applications can establish their baseline without any
pointer movement or scroll distance. Then emit the original uinput report.
The XTEST request shares the absolute-motion connection and completes before
the evdev write; no sleep or fabricated wheel notch is involved. No modifiers,
buttons, acceleration, axis increments, client deltas, or display settings change.

Device selection checks the exact event node obtained from the owned libevdev
uinput handle. It never selects by name. Device classes are queried afresh, so
reconnects, hotplug and changed XInput ids cannot use stale cached mappings.
XCB checked requests contain protocol errors locally rather than replacing the
process-wide Xlib error handler. Missing X11, unavailable devices, nonrelative
scroll axes, and device ids outside XTEST's seven-bit XI1 range retain the
original uinput behavior. The XTEST-only fallback backend is unchanged.

On Rocky 9.7's older libinput/Xorg stack, fractional high-resolution reports may
still be exposed to GTK only when their legacy detent accumulates. This change
preserves both reports; it does not convert smooth input to button events or
promise sub-detent application behavior that the installed X driver lacks.
