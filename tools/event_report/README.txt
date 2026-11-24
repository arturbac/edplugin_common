event_report 1 - Elite Dangerous events from Windows' logs, for a player to send back

What it does: it reads what Windows itself noted in the last 30 days about Elite Dangerous and your graphics card,
and writes it to a text file you can read before you send it:
- crashes and hangs of the game (EliteDangerous64.exe) and of its launcher (EDLaunch.exe), from Windows' Application
  log;
- graphics driver resets and errors, restarts without a clean shutdown and blue screens, from Windows' System log;
- Windows Error Reporting's saved reports of the game's crashes: the names of the files that were loaded into the game
  when it crashed (programs that put themselves into games, such as overlays or security software, show up there);
- the names, sizes and dates of the game's crash dumps (the dumps themselves are not read or included).
It shows the same text in its window. It takes a few seconds.

What it writes: one file beside the program, named by the time of the run, such as
event_report-20261124-131500.txt; each run gets a file of its own. The file holds the Windows version, the game's and
files' versions, graphics driver names, error codes and times, Windows' report ids and names of loaded files - no
paths. Your user name, your computer's name, e-mail addresses and account ids are replaced where the program finds them
(the file says how many); when your user name is shorter than 4 letters the program cannot look for it safely and
says so - then look for it yourself.

What it does not do: no network, nothing sent anywhere, no administrator rights, no change to Windows or to the game.
It reads only; the one file it writes is its result. Its own source is beside the program (event_report.cc and the
files beside it), the library's sources it uses in the edplugin_common folder; SOURCE.txt names the library's commit.

How to run:
1. If a checksum of the zip was posted with the link, compare it first: in PowerShell,
   Get-FileHash event_report-1.zip (SHA256). A different one: do not run it.
2. Extract the whole zip to a folder (not run from inside the zip).
3. Double-click event_report.exe. Windows may warn about an unknown publisher (the exe is not signed):
   "More info" -> "Run anyway".
4. When it says "press Enter to close", press Enter.
5. Open the event_report-<date>-<time>.txt it named (the newest one, when there are more), read it, and send it back
   if you are fine with what it says.

SHA256SUMS.txt lists the files' checksums; LICENSE is the library's license (MIT).
