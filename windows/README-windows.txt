PSBox Cloud Gaming for Windows - Portable Edition
=================================================

Xbox Cloud Gaming (xCloud) native client: browse the cloud catalog and stream
games with sound, with a controller (Xbox, DualSense, DualShock 4, 8BitDo,
Switch Pro...), keyboard and mouse.

You need a Microsoft account and a Game Pass plan with cloud gaming
(free-to-play games like Fortnite stream without it once on your account).
Unofficial project, not affiliated with, endorsed or sponsored by
Microsoft or Sony.


Running it
----------
Extract the zip archive to any folder and run:

    psbox.exe

Optional command-line parameters:
    --windowed      Start in windowed mode
    --fullscreen    Start in fullscreen mode
    --data-dir <dir> Store user data in custom folder

F11 or Alt+Enter toggles fullscreen; Settings > Display mode remembers it.

Self-test without signing in:
    psbox.exe --selftest C:\Temp\psbox-selftest


Your data
---------
Sign-in credentials, settings, caches and log (xcloud.log) are kept in:
    %LOCALAPPDATA%\psbox\ (or specified by --data-dir)
Nothing is written to the program folder. Deleting %LOCALAPPDATA%\psbox\
completely resets the app.


Controllers
-----------
Standard XInput controllers (Xbox One, Xbox Series, 8BitDo) work out of the box.
PlayStation DualSense and DualShock 4 controllers are supported via SDL3 USB/Bluetooth HID.
