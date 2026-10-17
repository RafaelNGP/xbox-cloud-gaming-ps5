PSBox Cloud Gaming for Linux - version @XC_PACKAGE_VERSION@
==========================================================

Xbox Cloud Gaming (xCloud) client: browse the cloud catalog and stream
games with sound, with a controller (DualSense, DualShock 4, Xbox,
Switch...), keyboard and mouse. A preview of the PS5 app's Linux build.

You need a Microsoft account and a Game Pass plan with cloud gaming
(free-to-play games stream without it once they are on your account).
Unofficial project, not affiliated with, endorsed or sponsored by
Microsoft or Sony.


Running it
----------
Unpack the archive anywhere and start PSBox/psbox:

    tar xf PSBox-@XC_PACKAGE_VERSION@-linux-x86_64.tar.gz
    PSBox/psbox                     # --windowed, --fullscreen, --data-dir <dir>

It runs from its folder: SDL3 is bundled in lib/ (psbox finds it on its
own) and the fonts and certificates are in assets/. Needs an x86-64
Linux with glibc 2.39 or newer (Ubuntu 24.04, Fedora 40, Debian 13 or
later), X11 or Wayland, and PulseAudio, PipeWire or ALSA.
F11 or Alt+Enter switches fullscreen; Settings > Display mode keeps it.

Menu entry: copy psbox.desktop to ~/.local/share/applications/ and
psbox.png to ~/.local/share/icons/, then point Exec= at this folder's
psbox, e.g.

    sed "s|^Exec=.*|Exec=$PWD/PSBox/psbox|" PSBox/psbox.desktop \
        > ~/.local/share/applications/psbox.desktop
    cp PSBox/psbox.png ~/.local/share/icons/psbox.png

Check without signing in (window, sound, input; screenshots in the
folder given):

    PSBox/psbox --selftest /tmp/psbox-selftest


Your data
---------
Sign-in, settings, caches and the log (xcloud.log) are kept in
~/.local/share/psbox ($XDG_DATA_HOME/psbox, or $XCLOUD_DATA_DIR / --data-dir).
Nothing is written to the PSBox folder. Signing out removes the sign-in;
deleting that folder resets the app.


Controllers
-----------
Basic input works on any pad SDL knows. The DualSense's (and DualShock 4's)
rumble, adaptive triggers, light bar, gyro and touchpad need read/write
access to its /dev/hidraw* device. Most desktops grant it to the logged-in
user (systemd's uaccess, Steam's udev rules). If they don't, add a udev
rule, e.g. /etc/udev/rules.d/70-dualsense.rules:

    KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0ce6", MODE="0660", TAG+="uaccess"
    KERNEL=="hidraw*", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0df2", MODE="0660", TAG+="uaccess"

then "sudo udevadm control --reload && sudo udevadm trigger" and plug the
pad in again. Options the PC can't honour (the PS5's upscalers, 120 Hz,
a light bar without a DualSense) are hidden.


Updates
-------
The app tells you in Settings > Updates when a new version is out and opens
its page; it doesn't update itself. Download the new archive and replace
the PSBox folder (your data stays in ~/.local/share/psbox).


Licenses
--------
PSBox is GPL-3.0-or-later (licenses/LICENSE). It links Mbed TLS,
libdatachannel, usrsctp, libSRTP, plog, FFmpeg (LGPL-2.1, see
licenses/FFmpeg-NOTE.txt), stb, and bundles SDL3 (zlib) and the Inter fonts
(OFL): their texts are in licenses/, the list in
licenses/THIRD_PARTY_NOTICES.md. Source code: the repository at this
release's tag, https://github.com/RafaelNGP/xbox-cloud-gaming-ps5
