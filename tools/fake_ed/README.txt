fake_ed 1 - a program in the game's place for the edloader plugins' tests

What it is for: testing edloader and its plugins on Windows (or under wine) without starting Elite Dangerous. It imports
D3D11CreateDevice from d3d11.dll as the game does, so Windows loads the d3d11.dll lying beside fake_ed.exe - edloader,
when it is there - and edloader loads the plugins of its list. Without a d3d11.dll beside it, Windows' own is used.

What it does: it makes a Direct3D 11 device as the game does at its start (twice, with no graphics card named and no
flags: first only asking, then making it), a swap chain on a small window shown on the screen, and draws frames in
five phases of 3 seconds each: without Direct3D's multithread protection, with it, after it is turned off, with
ClearState and Flush in each frame, and after the swap chain's pictures are resized. A frame sets the primitive
topology, draws no points and presents, about 30 to 60 frames a second. Nothing is drawn to the window: what it shows
is not defined (usually black). Closing the window is refused (said in the file) until the run ends. After the phases
one ClearState and Flush more, so the swap chain goes before its window. At the end, also after a step that failed, it
waits 2 seconds, so a plugin that writes a report every second writes its last one, and closes.

What it writes: each line at once to fake_ed-<date>-<time>.txt beside the exe (a file of its own for each run; the
name by the local time of the start, the lines' times in UTC, as the plugins' logs have them) and to the console:
where the exe's D3D11CreateDevice leads (edloader beside the exe or the system's d3d11.dll), each call's result code
(for Present the count of results other than S_OK and the first of them), the module holding the device's method
table, the multithread protection before and after each change, and for each phase its start, its end and the count of
each call. No user name, computer name or paths. Exit code 0 when every phase ran, 1 otherwise (a step failed: said in
the file), 2 when a call did not come back in 20 seconds (a plugin or an overlay hung: the file says which call; the
program ends itself).

Edloader's own files: edloader reads its list from %USERPROFILE%\edloader or from the folder in the variable
EDLOADER_DIR; a test sets EDLOADER_DIR to a folder of its own, so the usual installation's list is not used. Edloader
still adds one line, EDLOADER_DIR=..., to %USERPROFILE%\edloader\logs\edloader.log.

The runs under wine: tools/fake_ed/test.sh in the library's repository runs fake_ed.exe with edloader and
probe_plugin.dll (tools/probe_plugin) alone (r1) and, given EDHM's files, with EDHM before the probe (r2a) and after it
(r2b; EDHM's d3dx.ini copied with its [Loader] target set to fake_ed.exe, since 3Dmigoto does not start in an exe
other than its target), each run in a new folder with its own EDLOADER_DIR, and the wine version, the library's
commit, the sha256 of every file put in it and the files the run made in RUN.txt. Its header says what a run needs to
pass.

What it does not do: no network, no administrator rights, nothing written by itself but its result beside it; the
game is never started. It ends without the plugins' unloading (as context_probe), so a plugin that would hang there
does not hold the test.
