GRAPHIRE DRIVER 0.1 - for Wacom ET-0405A-U (Graphire2 4x5) on Windows 10/11
===========================================================================

A small tray program that reads the tablet directly and feeds Windows real
pen input (Windows Ink): 512 pressure levels, eraser, side buttons, the
Graphire mouse, absolute/relative modes, acceleration and pressure curves.
It needs no driver install and no driver signing.


USE
-----

The application will only appear as a tray icon. If you wish to change settings,
test pressure, or exit the applications, right-click on the tray icon.

SETUP
-----
1. Close OpenTabletDriver (if using): right-click its tray icon, choose Exit, and turn off
   its autostart. Only one program can drive the tablet at a time.
   If you ever used Zadig on the tablet, or an old Wacom driver is still
   installed, remove it. In Device Manager the tablet should appear as a normal
   "USB Input Device" / "HID-compliant ..." device.
   (VMulti can stay installed; this program doesn't use it.)

2. Put the files in a folder you can write to, e.g. C:\Tools\GraphireDriver

3. Run GraphireDriver.exe. It's unsigned, so Windows may show
   "Windows protected your PC". If it does, click More info, then Run anyway.
   A tablet icon appears in the system tray (check the ^ overflow area).

4. Right-click the tray icon, then choose "Status & pressure test...".
   Hover the pen and press on the tablet. The pressure bar should move.

5. Optional: right-click the tray icon, then choose "Start with Windows".


APP SETTINGS FOR PRESSURE
-------------------------
- Krita:          Settings > Configure Krita > Tablet settings >
                  "Windows 8+ Pointer Input"
- Photoshop CC:   uses Windows Ink by default
- Clip Studio:    File > Preferences > Tablet > "Use TabletPC"
- GIMP 2.10/3:    Edit > Input Devices: set the pen device to "Screen"
- Paint.NET, OneNote, Sketchable, Windows Whiteboard: work as is

Apps that only support Wintab (some very old software) won't see pressure.


SETTINGS
--------
Tray menu > Edit settings... opens graphire.ini in Notepad. Saved changes are
applied automatically. Useful ones:

  [Mapping] Mode           absolute (tablet = screen) or relative (like a mouse)
            Area...        use part of the tablet
            Monitor        0 = all screens, 1 = first, 2 = second...
  [Relative] Sensitivity / Acceleration / MaxGain
  [Pressure] Max           lower it (e.g. 400) to reach full pressure with a
                           lighter touch
             Curve         0.7 = softer, 1.5 = firmer
  [Buttons]  LowerButton / UpperButton = right, middle, left, double, barrel, none
  [Smoothing] Amount       0 to 0.9; takes out jitter but adds lag
  [Mouse]    settings for the Graphire mouse (puck)
  [Device]   Feedback=3    hides the Windows pen tap ripples


IF SOMETHING DOESN'T WORK
-------------------------
1. Right-click the tray icon and turn on "Log raw reports".
2. Hover the pen, draw a line, press each side button, try the eraser,
   and move the mouse puck.
3. Right-click the tray icon, choose "Open log", and send me graphire.log.

The log lists every Wacom device and interface found, whether the tablet
switched into Wacom mode, and the raw data. That's enough to fix most
problems.

Unplugging and re-plugging the tablet returns it to plain mouse mode.


BUILDING FROM SOURCE
--------------------
Written in C. It uses the Graphire protocol from the Linux wacom driver
(wacom_wac.c) and the Windows pen injection API (InjectSyntheticPointerInput).
Build it on Linux with mingw-w64, or on Windows with MSYS2:  ./build.sh
