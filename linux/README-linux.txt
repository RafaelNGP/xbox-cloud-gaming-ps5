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
Make the AppImage executable and start it (on the Steam Deck too, in
Desktop or Game mode):

    chmod +x PSBox-@XC_PACKAGE_VERSION@-linux-x86_64.AppImage
    ./PSBox-@XC_PACKAGE_VERSION@-linux-x86_64.AppImage     # --windowed, --fullscreen, --data-dir <dir>

SDL3, the fonts and the certificates are inside it. Needs an x86-64
Linux with glibc 2.39 or newer (Ubuntu 24.04, Fedora 40, Debian 13 or
later), X11 or Wayland, and PulseAudio, PipeWire or ALSA.
F11 or Alt+Enter switches fullscreen; Settings > Display mode keeps it.
Without FUSE, run it with APPIMAGE_EXTRACT_AND_RUN=1 set. A menu entry:
an AppImage launcher (AppImageLauncher, Gear Lever) adds one, or add the
AppImage to Steam as a non-Steam game.

Check without signing in (window, sound, input; screenshots in the
folder given):

    ./PSBox-@XC_PACKAGE_VERSION@-linux-x86_64.AppImage --selftest /tmp/psbox-selftest


Your data
---------
Sign-in, settings, caches and the log (xcloud.log) are kept in
~/.local/share/psbox ($XDG_DATA_HOME/psbox, or $XCLOUD_DATA_DIR / --data-dir).
Nothing is written inside the AppImage. Signing out removes the sign-in;
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
pad in again. With a Vulkan 1.3 driver, the app uses the same GPU compute
pipeline as the PS5 (FSR 1, Anime4K x2, Anime4K Restore, CAS sharpening and
block smoothing), falling back to SDL_Renderer otherwise. Options the PC
can't honour (the PS5's 120 Hz mode switch, a light bar without a DualSense)
are hidden.


Updates
-------
The app tells you in Settings > Updates when a new version is out and opens
its page; it doesn't update itself. Download the new AppImage and use it
in place of the old one (your data stays in ~/.local/share/psbox).


Licenses
--------
PSBox is GPL-3.0-or-later (licenses/LICENSE). It links Mbed TLS,
libdatachannel, usrsctp, libSRTP, plog, FFmpeg (LGPL-2.1, see
licenses/FFmpeg-NOTE.txt), stb, FSR 1 and Anime4K shaders (MIT), and bundles
SDL3 (zlib) and the Inter fonts (OFL): their texts are in PSBox/licenses/
inside the AppImage (--appimage-extract unpacks it), the list in
licenses/THIRD_PARTY_NOTICES.md. Source code: the repository at this
release's tag, https://github.com/RafaelNGP/xbox-cloud-gaming-ps5
