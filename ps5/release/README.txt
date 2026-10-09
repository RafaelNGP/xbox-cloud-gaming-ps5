PSBox Cloud Gaming - version @VERSION@
======================================

Native Xbox Cloud Gaming (xCloud) client for jailbroken PS5 consoles:
browse the cloud catalog and stream games at 1080p60 with sound, using
the DualSense as an Xbox controller. The picture is upscaled to 4K on
the PS5's GPU (AMD FidelityFX FSR 1, or Anime4K).

You need a Microsoft account; Game Pass Ultimate for most of the
catalog. Free-to-play games stream without it once they are on your
account (see "Free-to-play games").

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

Updating: when a new version is out, the app offers it after sign-in
("Update now" / "Not now"; also in Settings > Updates). It downloads it,
checks that it was signed by this project, installs it and restarts by
itself; your sign-in and settings are kept. By hand: close the app and
copy the new @TITLE_ID@ folder over the old one.


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
OPTIONS + TOUCHPAD during a game: the Xbox button (opens the Xbox
guide), the statistics line over the game, upscaling (FSR or AI, each
with or without "clean-up": Anime4K Restore takes the blur and noise of
compression off the picture first; "FSR + clean-up" is the default),
sharpness (off / low / medium / high: sharpens the picture's edges),
block smoothing (off / low / high, or auto: smooths the squares
compression leaves in dark, flat areas, stronger as the bitrate falls),
stream resolution (720p / 1080p, and 1440p where a stream delivered
it: the app measures it),
and leave the game. Circle closes it (and asks for a clean picture).
It also shows the connection (region, latency, bitrate, frame rate,
packet loss, and how long a frame takes from the network to the TV)
and the controllers in use.

Touchpad shortcuts: swipe down (or left) for this menu, up (or right)
for the Xbox button.

If the connection drops for a moment, the stream reconnects by itself
and the game goes on where it was.


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
the light bar in the colour of the game in focus, and the confirm button (Cross, or Circle as on Japanese consoles: it
then acts as Xbox A in games too). Saved in
/data/homebrew/@TITLE_ID@/settings.json.



My consoles (Remote Play)
-------------------------
The third tab (R1) lists your own Xbox consoles; Cross plays one: its
screen, games and apps, on the PS5. On the Xbox, turn on Settings >
Devices & connections > Remote features, and choose the Sleep power
mode so it wakes up by itself (it takes about ten seconds).

The Xbox button (game menu, or a swipe on the touchpad) opens the
Xbox's own guide. To stop,
use "End the stream" in the game menu (or stop it from the guide).
Turning the Xbox off from the guide in the middle of a stream can leave
its Remote Play stuck: the app then says so, and holding the console's
power button for 10 seconds, then turning it on, fixes it.


Free-to-play games
------------------
The home screen has a row of free-to-play games. Each one must be added
to your account once, for free: a game you don't have yet shows FREE,
and its page has a QR code of its store page. Scan it with your phone,
choose Get, then press Play. If you press Play first, the app says what
to do, and Cross tries again.


Your games
----------
Games your account owns (bought, or got free), then "Available to buy":
games that stream in the cloud once bought, with their store price. A
game's page shows a QR code of its store page: buy it on your phone (or
on xbox.com / in the Xbox app) and it appears in Your games.


Problems
--------
The app writes a log to /data/homebrew/@TITLE_ID@/xcloud.log. It holds
nothing of your account: no password, token, sign-in code, gamertag or
PS5 user name. Attach it when you report a problem:
https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/issues


License
-------
GPL-3.0-or-later: see LICENSE. The licenses of the bundled components are
in licenses/ (see licenses/THIRD_PARTY_NOTICES.md). Source code:
https://github.com/RafaelNGP/xbox-cloud-gaming-ps5 (tag @VERSION@)
