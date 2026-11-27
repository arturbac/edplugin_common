context_probe 7 - a small diagnosis for the edloader plugins on Windows

What it does: on each of your graphics cards, and once on the card Windows picks for a program that names none, it makes
a Direct3D 11 device (as a game does at its start, with no drawing), looks where Windows keeps the tables of the
device's and the device context's methods, whether Windows rewrites them, and whether another program changed them (and
to which module that change leads), also inside the code of Windows' d3d11.dll, and to which files (Windows', the
graphics driver's) the methods past the known ones lead. Then it puts hooks on the device context as the plugins do,
with the plugins' own library code: three pretend plugins, each on four methods (Draw, IASetPrimitiveTopology,
ClearState, Flush); it calls these methods (a Draw of no points: nothing is drawn), turns Direct3D's multithread
protection on and off, and writes what the library sees after each step. Then, as a game does next, it makes a swap
chain (what a game shows its pictures through) on a window that is never shown, presents a few empty pictures to it
without drawing anything, resizes its pictures once, and looks again: at the swap chain's table, at what changed in the
device's tables, and at which files came into it (overlays often join only at this point). It also lists the names of
the files loaded into it from outside the Windows folder - programs that put themselves into every program (overlays,
security software). It writes what it found beside the exe, to a file named by the time of the run, such as
context_probe-20261124-131500.txt; each run gets a file of its own. It takes about 10 seconds per card (a picture
that does not come back in 10 seconds ends the look at the cards left, and is said in the file).

What it does not see: it is not the game, so an overlay that joins only a game it knows (by the game's program name,
or when the game is started from its launcher or Steam) is not seen. "Nothing found" means nothing found in this
program, not that nothing changes the game.

What it does not do: no network, nothing drawn, no window on the screen (the window it makes is never shown), no
administrator rights. The program itself writes no file but its result beside it (an earlier run's result is never
replaced); the graphics driver may keep its usual caches, and its software (such as GeForce Experience or Radeon
Software) may list context_probe.exe among the programs it has seen. The file holds the Windows and d3d11.dll versions,
your graphics cards' names and numbers, and file names of loaded modules - no user name, computer name or paths. Its own
source is beside the exe (context_probe.cc and the files beside it), the library's sources it uses in the
edplugin_common folder; SOURCE.txt names the library's commit.

How to run:
1. If a checksum of the zip was posted with the link, compare it first: in PowerShell,
   Get-FileHash context_probe-7.zip (SHA256). A different one: do not run it.
2. Extract the whole zip to a folder (not run from inside the zip).
3. Close the game. Leave your usual programs (overlays, security software) running: what they change is what is looked
   for.
4. Double-click context_probe.exe. Windows may warn about an unknown publisher (the exe is not signed):
   "More info" -> "Run anyway". Security software may warn too: the program reads the memory of the modules loaded
   into it, points a device context at tables of its own, and makes a little executable memory for the hooks' jumps.
5. When it says "press Enter to close", press Enter.
6. Send the context_probe-<date>-<time>.txt it named from that folder back (the newest one, when there are more).

SHA256SUMS.txt lists the files' checksums; LICENSE is the library's license (MIT).
