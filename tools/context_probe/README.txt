context_probe 5 - a small diagnosis for the edloader plugins on Windows

What it does: on each of your graphics cards it makes a Direct3D 11 device (as a game does at its start, with no
window and no drawing), looks where Windows keeps the tables of the device's and the device context's methods, whether
Windows rewrites them, and whether another program changed them (and to which module that change leads). It also
lists the names of the files loaded into it from outside the Windows folder - programs that put themselves into every
program (overlays, security software). It writes what it found to context_probe.txt beside the exe. It takes a
few seconds per card.

What it does not do: no network, no drawing, no change to any file but context_probe.txt beside it, no
administrator rights. The file holds the Windows and d3d11.dll versions, your graphics cards' names and numbers, and
file names of loaded modules - no user name, computer name or paths. Its own source is beside the exe
(context_probe.cc and the files beside it); the rest is the edplugin_common library's (memory, paths, file).

How to run:
1. Extract the whole zip to a folder (not run from inside the zip).
2. Close the game. Leave your usual programs (overlays, security software) running: what they change is what is looked
   for.
3. Double-click context_probe.exe. Windows may warn about an unknown publisher (the exe is not signed):
   "More info" -> "Run anyway".
4. When it says "press Enter to close", press Enter.
5. Send context_probe.txt from that folder back.

SHA256SUMS.txt lists the files' checksums.
