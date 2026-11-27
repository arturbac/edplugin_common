probe_plugin 1 - an edloader plugin for the plugins' tests that only watches

What it is for: testing how the plugins' context hooks (the library's hook module) keep in a chain of edloader
plugins, with fake_ed (tools/fake_ed) in the game's place, alone or beside other plugins such as EDHM (3Dmigoto). It
is not for playing.

How it is put in: probe_plugin.dll in edloader's plugins folder and its name on a line of edloader.txt, as any plugin.
It passes every d3d11 call on to the next one in the chain (under edloader "d3d11.dll" by name, which is edloader;
without edloader Windows' own d3d11.dll).

What it does: at the first device made through it, it writes which d3d11 proxies stand before it in the chain (by
their product, description and version; 3Dmigoto is named, and the plugin goes on), which program module asked for
the device, which object the device's immediate context is (Direct3D's own, in Windows' d3d11.dll, or a wrapper's,
such as 3Dmigoto's), puts the hooks of three pretend plugins on that context as the plugins do (context_probe's code:
two by the library's rule, one with a table of its own, each on Draw, IASetPrimitiveTopology, ClearState and Flush,
counting their calls), and then every second writes how many calls each counted since the last report, the
multithread protection the library tells, the entries of the runtime's table changed since the attach and the classes
the library's watches see. A later device is said and left alone.

What it does not do: it draws nothing, changes nothing on the screen and sets no state of the device or its context;
it only asks them (GetImmediateContext, and through the library QueryInterface, AddRef, Release and
GetMultithreadProtected, the last also every second from its own thread). No network.

What it writes: probe_plugin-<date>-<time>.txt in edloader's folder of logs (EDLOADER_LOG_DIR; beside the dll without
edloader), a file of its own for each run (the name by the local time of the start, -2 and on when the name is taken),
each line with its UTC time appended at once; the library's own lines in probe_plugin.log beside it. The process's exe
name and module names are in it; no user name, computer name or paths. When no file can be made, it says so to a
debugger (OutputDebugString).
