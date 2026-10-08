PSBox Cloud Gaming - version @VERSION@
======================================

Native Xbox Cloud Gaming (xCloud) client for jailbroken PS5 consoles:
browse the cloud catalog and stream games at 1080p60 with sound, using
the DualSense as an Xbox controller. The picture is upscaled to 4K on
the PS5's GPU (AMD FidelityFX FSR 1).

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
attach it to a bug report. Signing out (hold TOUCHPAD for 5 seconds
on the home screen) deletes it.


Controls
--------
Menus:   D-pad or left stick to move, Cross to select, Circle to go back,
         L1 / R1 to switch between Game Pass and Your games,
         Triangle to search the current tab (Square deletes a letter),
         OPTIONS for Settings, hold TOUCHPAD for 5 seconds to sign out.
In game: the DualSense acts as an Xbox controller
         (Cross = A, Circle = B, Square = X, Triangle = Y,
          OPTIONS = Menu, TOUCHPAD = View); games that vibrate the
         Xbox triggers vibrate the DualSense's triggers.
         OPTIONS + TOUCHPAD opens the game menu (below).
         When a game asks for text, the PS5 keyboard opens.


Game menu
---------
OPTIONS + TOUCHPAD during a game: resume, the statistics line over the
game, sharpness (off / low / medium / high: sharpens the picture's
edges), block smoothing (off / low / high: smooths the squares
compression leaves in dark, flat areas; low keeps textures best),
stream resolution (720p / 1080p / 1440p, changed in a few seconds),
refresh the picture, and leave the game. It also shows the
connection (region, latency, bitrate, frame rate, packet loss) and the
controllers in use.


Local multiplayer
-----------------
Up to four players: turn on another DualSense and sign in to the PS5
with another user; the game gets it as the next Xbox controller. The
controllers in use are shown, numbered in their light-bar colours, at
the bottom left of the home screen and in the game menu. Whether a game
accepts a second player is up to the game.


Settings
--------
OPTIONS on the home screen; Cross opens a list of choices: language
(English, Portugues (Brasil), Espanol, Francais, Deutsch, Italiano),
stream resolution (720p for slower connections, 1080p, or 1440p -
experimental, only granted where Microsoft offers it, otherwise you
still get 1080p), server region (Automatic, or a specific Azure
region, with the latency measured in your past sessions), the sticks'
dead zone (raise it if a stick drifts), trigger vibration on or off,
and the confirm button (Cross, or Circle as on Japanese consoles: it
then acts as Xbox A in games too). Saved in
/data/homebrew/@TITLE_ID@/settings.json.

When a new version is out, a notification says so after sign-in.


Your games
----------
Games your account owns outside Game Pass, then "Available to buy":
games that stream in the cloud once bought, with their store price. A
game's page shows a QR code of its store page: buy it on your phone (or
on xbox.com / in the Xbox app) and it appears in Your games.


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
