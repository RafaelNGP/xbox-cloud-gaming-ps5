PSBox Cloud Gaming - version @VERSION@
======================================

Native Xbox Cloud Gaming (xCloud) client for jailbroken PS5 consoles:
browse the cloud catalog and stream games at 1080p60 with sound, using
the DualSense as an Xbox controller.

You need a Microsoft account with a subscription that includes cloud
gaming (Game Pass Ultimate).

Unofficial project, not affiliated with, endorsed or sponsored by
Microsoft or Sony.


Requirements on the PS5
-----------------------
- A homebrew environment that runs directory-style apps from
  /data/homebrew (e.g. kstuff with ShadowMountPlus).
- A way to copy files to the PS5 (e.g. an FTP server).
- Internet access; a wired connection or 5 GHz Wi-Fi works best.


Installation
------------
1. Copy the @TITLE_ID@ folder (the one holding this file), as a whole, to
   /data/homebrew/ on the PS5.

2. Wait for the loader (e.g. ShadowMountPlus) to add the
   "PSBox Cloud Gaming" icon to the Home screen, then launch it.

Updating: close the app and copy the new @TITLE_ID@ folder over the old
one. Your sign-in is kept.


First launch
------------
The app shows a code. On your phone or computer, open
https://www.microsoft.com/link (or scan the QR code on screen) and enter
that code. Your password is never typed on the console.

The sign-in is saved in /data/homebrew/@TITLE_ID@/account.json. That
file gives access to your Microsoft account: never share it, and never
attach it to a bug report. "Sign out" (OPTIONS on the home screen)
deletes it.


Controls
--------
Menus:   D-pad or left stick to move, Cross to select, Circle to go back,
         Triangle for Settings, OPTIONS to sign out.
In game: the DualSense acts as an Xbox controller
         (Cross = A, Circle = B, Square = X, Triangle = Y,
          OPTIONS = Menu, TOUCHPAD = View).
         Hold OPTIONS + TOUCHPAD for one second to leave the game.


Settings
--------
Triangle on the home screen: language (English, Portugues (Brasil),
Espanol, Francais, Deutsch, Italiano), stream resolution (1080p, or 720p
for slower connections) and server region (Automatic, or a specific
Azure region). Saved in /data/homebrew/@TITLE_ID@/settings.json.


Problems
--------
The app writes a log to /data/homebrew/@TITLE_ID@/xcloud.log (no
passwords or tokens in it). Attach it when you report a problem:
https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/issues


License
-------
GPL-3.0-or-later: see LICENSE. The licenses of the bundled components are
in licenses/ (see licenses/THIRD_PARTY_NOTICES.md). Source code:
https://github.com/RafaelNGP/xbox-cloud-gaming-ps5 (tag @VERSION@)
