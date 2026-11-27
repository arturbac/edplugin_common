# edplugin_common

A static C++23 library shared by the Elite Dangerous d3d11 plugins: edloader, edworld, edupscale, edwide_screen and
edshader-catch. Every plugin's dll links its own copy of the code and has its own state. The library does not depend
on any particular loader.

License: MIT (see `LICENSE`).

## What it holds

- `include/edplugin_common/core/lock.h` - `exclusive_lock_t`, `shared_lock_t`: an SRW lock held for a scope. Each one
  is noted on a short stack of the thread (8 places, constant-initialised `thread_local`) while it is held, and its
  destructor takes its own entry off wherever it stands (a lock let go out of order, or in an unwind), so the guard
  (below) lets go of exactly the locks a faulted body still held; past the 8 places a lock is only counted, and a
  fault while one such is held cannot let it go.
- `include/edplugin_common/core/once_flag.h` - `once_flag_t`: `first()` is true for the first caller only (until
  `reset()`), also from several threads; constant-initialised, so a global one does no work when the dll is loaded.
- `include/edplugin_common/text/format.h` - for `std::format`: `wide()` (a wide string in a narrow line, as UTF-8),
  `utf8()` (a UTF-8 string in a wide text, as UTF-16; bytes that are not UTF-8 become U+FFFD), `as_unsigned()` (a
  signed value or an enumeration as its bits, for hexadecimal fields), and `try_format_into(buf, [size,] fmt, ...)`:
  `std::format` into a fixed buffer, cut at its end and NUL-terminated, never thrown; the characters written, or no
  value when the text could not be made (the buffer then empty), so the caller logs it. A plugin keeps its own
  `format_into` over it with its log; the library's function has another name, so a plugin's `format_into` called
  unqualified with an argument of the library's types does not find it by argument-dependent lookup.
- `include/edplugin_common/log/log.h` (code in `src/log.cc`) - a plugin's log, one per module:
  - `log_open(dir, name)`: `<dir>\<name>.log` for this game session. A non-empty file there from the last session
    is first set aside as `<dir>\<name>\<name>.<UTC of its last write>.log` (the directory made when missing);
    two sessions whose last writes fall in the same second get one name, and the second is not set aside. When
    the file cannot be set aside (the name taken, a file where the directory would be, a directory's path over 247
    characters or the moved file's over 259, or another error), the session appends to it and logs why, with the
    error's code. A second call while the log is open does nothing and is true; else false, said by
    `OutputDebugString`, for an empty directory or name, a log's path over 259 characters or a file that cannot be
    opened. Older sessions are not removed.
  - `log_line(fmt, ...)` / `log_vline(fmt, args, tap)`: one line, a UTC time stamp with milliseconds, the text and a
    line end, cut to 2 KB and written with Windows' own calls under an SRW lock; a text that cannot be made is cut
    where it failed and says so; never thrown. The optional tap (`line_tap_t`) gets each line's text (without stamp
    and line end) once, before it is written, outside the log's lock, also in a batch and when the log is not open.
  - `log_batch_begin()` / `log_batch_end()`: one thread's lines gathered (16 KB) and written at once, at the end or
    when the batch is full; another thread's lines are written at once meanwhile; batches do not nest (the first
    end ends one). The thread ends its batch on every way out, a fault too (the guard ends it after a fault or a C++
    exception, also a batch the caller began before the guarded call): a batch never ended keeps its lines and stays
    that thread's for good.
  - `log_line_to(path, fmt, ...)`: one such line appended to another file (opened, written, closed).
  - `log_flush()`: what is written so far, this thread's batch too, flushed to the disk (`FlushFileBuffers`), for a
    line that has to stay when the whole system stops soon after (a GPU hang or reset); a process's end alone loses
    no written line. Slow, every thread's lines wait for it: for a rare line. False without an open log.
  - `log_is_open()`; `log_close()` for tests.
  - `log_break()`: the log off for the rest of the session, no line made or written from then, said once by
    `OutputDebugString`; the guard calls it after a fault in the log's own write. `log_is_broken()`.

  Every global of the log is constant-initialised and trivially destructible, so the library adds no constructor or
  destructor to a dll. Nothing of it may be called from `DllMain`, and the log is not closed at
  `DLL_PROCESS_DETACH` (the game's threads are killed at its exit).
- `include/edplugin_common/log/state_log.h` - `state_log_t<value_t>`: the last value of a state (an integer or an
  enumeration, lock-free); `changed(now)` is true when it differs from the last one, so `if(s.changed(v))
  log_line(...)` logs once per change of state, not once per frame. The first value, given to the constructor, is
  the caller's "unknown".
- `include/edplugin_common/paths/paths.h` (code in `src/paths.cc`) - `environment(name)` (a value of any length,
  empty when not set), `module_path(module)` (a long path too; `nullptr` is the exe), `directory_of(path)` and
  `file_name_of(path)` (around the last `\` or `/`, copies), the names of the variables edloader sets for its plugins
  (`edloader_config_dir_variable`, `edloader_log_dir_variable`) and `plugin_dir(variable, self)` (the variable's
  folder, without it the plugin's dll's own), and `game_build_key()`: the game's build as the plugins name folders by
  it, `<the exe's size, low 32 bits>-<its last write, 16 hex digits>`, told once in the process, empty when the exe
  cannot be read (a long path too).
- `include/edplugin_common/file/file.h` (code in `src/file.cc`) - `read_text(path)`: the file whole, empty when there
  is no such file, no value when it is there but cannot be read (so a caller never takes it for an empty one);
  `write_time(path)` (0 without the file); `write_whole_file(path, data, bytes)`: made anew, a file not written whole
  removed, a failure one line in the module's log; `append_to_file(path, data, bytes, mode)`: `create_or_append` (by
  default), `create_new` (never a file that is there; not written whole: taken away through its own handle) or
  `append_existing` (held unshared, written at its end; not written whole: cut back to its length), false with
  `GetLastError()` the cause; `map_file_rw(path, bytes,
  tag)`: a file shared with other processes (made when missing, its directory not; grown to `bytes`), a failure one
  line `<tag>: cannot open|map|view ...`; `map_file_ro(path, least, most)`: another process's file, none when missing
  or smaller than `least`, silent. The views of both stay for the process's life.
- `include/edplugin_common/memory/memory.h` (code in `src/memory.cc`) - `readable(p, bytes)` (committed, read access,
  no guard page, the whole range in one region), `executable(p)`, `module_of(p)` (the reference count unchanged),
  `section_of(module, p, name)` (read under the guard: a module unloaded meanwhile gives `-`), and `first_export_with_prefix(module, prefix, name, fault)`: the first export by such
  a name, read through `pe_image` (below) under a fault handler (none, found, malformed, or a fault with its code).
- `include/edplugin_common/memory/pass_modules.h` (comparison in `src/pass_modules.cc`) - the modules passed over when a
  plugin tells who called it: a program that puts itself in the game's own calls (an antivirus's hooks), so a call
  seems to come from it. `default_pass_modules` is `bdhkm64.dll` (Bitdefender's); `next_pass_name(rest)` reads a list
  of dll file names by commas, a name at a time without the spaces and tabs around it (no Windows header, checked
  natively); `passed_over(file, list)`: whether a dll's file name is one of the list's, without regard to case
  (Windows' ordinal comparison), an empty name matching none.
- `include/edplugin_common/memory/pe.h` (code in `src/pe.cc`) - a loaded module's PE image read as data from outside
  the plugin. `pe_image(module[, loader_size])` gives a `pe_image_t`, or none when the headers are not those of a
  PE32+ image within its memory (an MZ header, `e_lfanew` in [64, 4096], the NT signature and magic, an optional
  header long enough for its data directories, at most 96 sections, the section table inside the headers). Its bound
  is the least of `SizeOfImage`, the loader's size when given and the memory of the image's allocation
  (`size_differs()` when they do not agree; the allocation is followed for 4096 regions at most); the headers' part
  ends at `SizeOfHeaders` or at the first section, whichever comes first (`headers()`). The headers and the section
  table are read once, as copies (a `pe_image_t` is about 4 KB). Every read keeps to the bound and to the headers or
  one readable section: `read<value_t>(rva)` copies the value out (no pointer to a header structure, no unaligned
  read); `span(rva, bytes)` and `name_at(rva[, most])` (its NUL in the same part), and the names the walks below give,
  are views of the module's memory, valid while it stays loaded. A section is read by its `IMAGE_SCN_MEM_READ`, not
  its pages' protection now: a page the game guards (`PAGE_GUARD`) read under the guard comes back as a fault (and the access takes the guard off the page).
  Also `section_of(rva)`, `directory(index)`, `sections()`. Over it: `for_each_export(image, f)` (each export by name
  with its RVA and whether it is a forwarder; false for a directory out of shape, a name without its NUL within 4096
  characters too), `export_address(image, name[, depth])` (no `GetProcAddress`, which may be hooked; a forwarder
  followed by the module `GetModuleHandleA` gives for its dll's name, `depth` times; one by ordinal not; a forwarder
  to an API set is not checked by a test) and `for_each_import(image, f)` (each import with its dll, its name or
  ordinal and its slot, given only when 8-aligned, past the headers and in the IAT directory when the image has one;
  up to the first descriptor with no name or no slots, where the loader stops; a thunk by name with any of bits 31-62
  set is out of shape; a walk's counts: descriptors, without names, slots refused, past the directory's size, out of
  shape). The module is not pinned: another thread unloading it while it is read faults, so a caller reads under the
  guard and skips the module then.
- `include/edplugin_common/memory/pe_range.h` - the ranges a PE image's offsets keep to (`rva_in`, `array_in`,
  `rva_inside`, `lfanew_ok`), without overflow, `constexpr` and without a Windows header.
- `include/edplugin_common/shader/shader.h` - the names of shaders, without a Windows header: `fnv1a64(data, bytes
  [, h])` (FNV-1a 64 from the plugins' basis 1469598103934665603 - a digit short of FNV's published one, kept since
  every name made so far uses it; `h` goes on from a hash so far), `fnv_3dmigoto` (3DMigoto's FNV-1 from 0) and
  `migoto_name_text` (`3dmgt_` and 16 hex digits). `include/edplugin_common/shader/names_of.h` (code in
  `src/names_of.cc`): `names_of(code, bytes)`, both names under a fault handler, `read` false when the bytes could not
  be read.
- `include/edplugin_common/dxgi/format.h` - `is_integer(format)`: a UINT or SINT format.
- `include/edplugin_common/dxgi/device_removed.h` (`src/device_removed.cc`) - a lost device told once for the module
  in the log, the line flushed (`log_flush`): `note_present_result(chain, hr)` after a `Present` or `Present1` (only a
  failure's code does anything: then the swap chain's device's `GetDeviceRemovedReason`; logged for
  `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET` or a reason other than `S_OK`), `check_device_removed(device)`
  for a plugin without a `Present` of its own, which calls it at most once a second (`GetDeviceRemovedReason` not
  `S_OK`); the line names both codes (`device_code_name`, no Windows header). No other D3D call.
  `device_loss_logged()`.
- Fault handlers: a `__try` catches a fault only of what it calls (clang-cl without `/EHa` does not catch a fault of an
  instruction in the `__try` body itself), so the reads under one are in a function of their own, not inlined
  (`names_of` and `first_export_with_prefix`, each with a `__try` of its own; the guard's body, called through a
  pointer), with no object to unwind in the function with the handler. Every
  filter of the library decides by `fault_action(code)` (`guard/guard.h`): the codes that are no fault go on to the
  next handler (a thread's name `0x406D1388`, `DBG_PRINTEXCEPTION_C` and its wide form; a breakpoint and a single
  step only with a debugger attached; UBSan's trap in a build with `EDPLUGIN_COMMON_UBSAN_TRAP`), every other code is
  handled. A code let through a `noexcept` function and unwound by a handler further out ends the process
  (`0xC0000409`); a handler further out that continues (`EXCEPTION_CONTINUE_EXECUTION`, as a debugger does) is fine.
- `include/edplugin_common/guard/guard.h` (code in `src/guard.cc`) - a plugin's code under a fault handler: a fault of
  the processor (an access violation, a stack overflow, a jump into nothing) or a C++ exception in it comes back as a
  `fault_t` (the code, the place and its module, an access violation's kind and address, a C++ exception's `what()`)
  instead of ending the game. The guard's `__try` is in `src/guard.cc`, around a call through a pointer. Without
  `/EHa` a destructor in the frames a fault leaves runs or not, as the compiler made the call the fault came through
  (one that may throw: it runs; one known not to: it does not); so after a fault the guard, in this order, sets the
  stack's guard page again after an overflow, lets go of the library's locks (`core/lock.h`) those frames still held,
  from the top, and ends the thread's log batch (under a guard of its own: a fault in the log's write turns the log
  off, `log_break`). Anything else the frames held (flags, buffers, raw locks) is the caller's handler's to set
  right. A fault in another module (the system, a driver) may leave that module's own locks held, and a part that
  goes on could then hang the game: `guard_t` turns the part off at once. A fault under a nested guard ends the
  thread's log batch, also one the outer code began. The original function
  (the next element of a chain, d3d11, the game's code) is never put under a guard: a fault there is the game's to
  report. Not caught: `__fastfail`, `/GS` and CFG failures, a heap found broken.
  - `run_guarded(f, fault)`: `f()` under the handler, its C++ exceptions caught too (the log batch ended after one, as
    after a fault); false after either.
    `run_guarded_call(body, arg, fault)` is the same for a plain function pointer.
  - `fault_line(fault, text)`: the fault as one line, `<what> at <module>+0x<offset>`, with the locks let go.
  - `guard_t(name, limit)`: a part of a plugin (constant-initialised). `run(f[, on_fault])`: `f()` under the guard
    unless the part is off; after a fault the line is logged (the first 16 of a session, with `n of limit`), then
    `on_fault(fault)` is called under a guard of its own (a fault in it turns the part off). After `limit` faults (0:
    no limit), at once after a stack overflow, a fault while a lock past the thread's note was held, or a fault in a
    module other than the plugin's (the one the library is linked into), the part is off for the session, said
    once; `turn_off(why)`, `broken()`, `faults()`.
  - `run_guarded_thread(name, body, arg[, attempts])`: a thread's body started again after a fault, a second apart,
    `attempts` times at most (8 unless given, below 1 taken as 1); a stack overflow ends it at once; each fault and
    the end logged.

  `src/guard.cc` is compiled with `-fno-lto` (the `__try` stays around a call of its own whatever the plugin's build
  does); a target in the top directory of the build with `INTERPROCEDURAL_OPTIMIZATION` set (not its per
  configuration form, not a target in a subdirectory) is a warning at configure, since a `__try` of its own is not
  kept out of LTO.
- `include/edplugin_common/settings/settings_doc.h` (code in `src/settings_doc.cc`) - a plugin's `doc/settings.md`
  from its own table of settings, one template for every plugin:
  - the plugin gives each setting as text: `setting_row_t` (name, default, group as an index, read at the start or
    while the game runs, range, what it does), and its title, intro and groups in `settings_doc_t`;
  - `settings_doc(doc, rows)`: the doc, the template byte for byte in the header's comment (by group, the rows read
    while the game runs first, a table each, `None.` for none); `|`, line ends and `<`, `>` outside code spans
    escaped; an error for a group out of range, a name twice or an empty name;
  - `write_settings_doc(variable, doc, rows)`: written to the file the environment variable names (nothing
    without it); the status, the error, the system's error code and the path come back for the plugin's log.
  Neither throws. A plugin's `tools/test.sh` sets the variable and compares the file with its `doc/settings.md`.
- `include/edplugin_common/ini/` (code of the watch in `src/ini.cc`) - a plugin's ini by its one table of settings,
  the same rules in every plugin:
  - `ini_text.h` (no Windows header): a line split into its key, `=` or `+=` and value (`;`, `#`, `[` at its start: a
    comment); the comment after a value by its kind: numbers, switches, hexadecimal names, colours, lists, sizes and
    choices at the first `;` or `#`, plain text only after a space or a tab, a path, a folder or a dll's name never;
    the readers (`read_float` as `strtof`, `read_uint32`, `read_bool`, `read_hex64` with an optional `0x` and leading
    zeros, at most 16 significant digits, 0 taken or refused, `read_hex24`, `read_hex_list` by commas, spaces or
    tabs); a UTF-8 byte order mark passed over, UTF-16 told; the file's own line end; UTF-8 and UTF-16 both ways.
  - `ini_table.h` (no Windows header): the table `ini_table_t` - rows `ini_setting_t` (name, field, group, read at
    the start or while the game runs, range, `about`, `doc_more`, `ini_note`, a text's kind), the withdrawn keys
    (`ini_withdrawn_t`, a note of their own once a session), the comment over a key in the ini (`about` or own), a
    list's line with a bad word refused or its good names taken, 0 as a name taken or refused. `read_ini_text` from the
    plugin's defaults with notes in one form for every plugin (the header's comment) and the line that set each row;
    `at_start_changes` (a start-only setting changed while the game runs, once a session); `ini_completion` (the
    blocks of the settings a file names nowhere, also not as a comment, by its line ends); `ini_doc_rows_t` (the rows
    for `write_settings_doc`). The library holds the values' form, the plugin their meaning (its clamps after a read).
  - `live.h` (no Windows header): `live_settings_t`, two slots and an atomic pointer; `publish` copies the live rows
    field by field into the slot not in use and adds one to its generation. The start-only fields never change after
    `start`: any thread may read them; the live fields and the generation only the publishing thread.
  - `ini.h`: `complete_ini` (a new file `create_new` with `\r\n`, one that is there `append_existing` by its line ends;
    UTF-16 and a file that cannot be read left alone), `read_ini_file`, `ini_watch_t` (at most once a second by the
    last write; a file taken away is said once and leaves the settings as they are; `ask_soon` from any thread).
  Nothing in it logs; the plugin logs the notes and results. A key taken out of a plugin goes to its withdrawn keys;
  a key whose type or meaning changes gets a new name, the old one withdrawn.
- `include/edplugin_common/hook/` (code in `src/context.cc`, `src/own_table.cc`, `src/slot.cc`, `src/watch.cc`,
  `src/multithread.cc`) - a plugin's hooks on the game's immediate context, kept when Windows' d3d11 rewrites the
  entries of its own method table (it does so when it switches its variants of the methods):
  - `context_rule.h` (no Windows header, its cases as `static_assert`s) - `context_pick(input)`: on a table of the
    plugin's own or in place, from the table's memory (image, private, unknown), its module (the system's d3d11.dll,
    another, none), the modules of its first 96 entries, the mark of another copy of the library, the interfaces
    `ID3D11DeviceContext1..N` the object answers as itself, the table's length (at least 115, 134, 144, 147 or 149
    entries for N = 0..4, fewer than 2048) and the plugin's setting (`context_ask_e`: automatic, in place, own
    table). In place for unknown memory, another module's image (3Dmigoto, ReShade), another copy's table, or fewer
    than 3/4 of the first 96 entries in modules; the point of the rule is named in the log (`point_name`).
  - `contract.h` - `hook_contract_version` (1), said in the log and written into the mark of a table of the
    library's own. What a plugin may count on from another's hooks: a stub jumps through the fixed address of its
    slot in the table the context came with, never through the object's current table; a hook's original on a table
    of its own is the slot's stub, called with the same `this`; nothing is freed, restored or released (the table,
    the stubs, the hooks, the context's reference stay to the end of the process); the context is attached only in
    the path that makes the device, the plugin nearest the runtime first; a plugin loaded later does not attach and
    says so. A plugin takes no lock of the runtime and works only while its multithread protection is off.
  - `own_table_mark.h` (no Windows header) and `own_table.h` - a table of the library's own for one context: the
    words before entry 0 copied, the entries in private read-write memory, and in private read-execute memory a
    stub per entry (`mov rax, <the runtime's entry's address>; jmp [rax]`, 16 bytes) after a mark of a fixed layout
    (magic `edpc_tbl`, the layout's version, the contract's version, the runtime's table, the entries, their count,
    stub 0 and the step). `make_own_table`, `own_table_put` (the stub returned as the original),
    `own_table_point` (the object pointed at the table after every hook is in), `find_mark`: a table of another copy
    is recognised through at most 4 allocations its first entries without a module lead into; any other layout of the
    mark is a foreign table.
  - `slot_plan.h` (no Windows header) and `slot.h` - `patch_slots(table, list)`: every page of the list made writable
    (write added, execute kept) before the first write and given back after the last, each slot read back; a page that
    cannot be used gives back the pages already changed and nothing is written. Each slot's result (`patched`,
    `already_ours`, `not_writable`, `read_back_differs`) and its original.
  - `context.h` - `context_hooks_t::attach(context, ask, list)`: the table read (each entry under the fault handler,
    up to 2048 leading to code), the way picked and said once in the log, the hooks written on a table of its own or
    in place (in place too when the table of its own cannot be made, as when the stubs cannot be made executable).
    A slot of the list past the table's entries (on another copy's table, past its mark's count) leaves the table
    as it is. Until the whole list is in, `works(this)` is false and every hook only passes its call on; a list
    written in part stays so. A context attached in whole gets a reference, never released. One try per plugin: a
    second `attach` (another context, or any after a try that failed) is `refused` before anything changes, said
    once in the log; the same context attached in whole is `already`. Another context than the one attached (a
    second device's) always passes. `watch_setup(list)` for the watch.
  - `watch_class.h` (no Windows header) and `watch.h` - `context_watch_t`: the watched slots' entries and the table
    the object points at, read cheaply and classed only when they changed: a slot is ours, covered by another hook
    (said once), taken (our original or one of our stubs back) or taken by the runtime (the runtime's entry, or any
    other entry into the system's d3d11); a table not ours is a plugin above on a table of its own or another hook's
    (said once), or the runtime's (the object re-pointed). Taken, taken by the runtime and re-pointed turn the plugin
    off. For the census: the runtime entries a table of its own followed, and the slots whose hooks had no call for a
    while.
  - `multithread.h` - `multithread_t`: the runtime's multithread protection from the context's
    `ID3D11Multithread`, else the device's (none: off), asked at `start` and again at `refresh()` (from any thread;
    exactly one caller sees a change); on turns the plugin off. Never set and never taken.
- `include/edplugin_common/chain/system_d3d11.h` (code in `src/system_d3d11.cc`) - `system_d3d11()`: the system's
  d3d11.dll loaded once by its full path from the system directory (never by name, which under a loader is the
  game's own copy), kept to the end of the process, and the range of its image; not from `DllMain`.
- `cmake/clang-cl-msvc-sdk.cmake` - the toolchain: a cross build for 64-bit Windows on Linux with clang-cl and LLVM's
  tools against MSVC's and the Windows SDK's headers and libraries, from the directory `MSVC_ROOT` (cache or
  environment variable) in clang's `/winsysroot` layout (`VC/Tools/MSVC`, `Windows Kits/10`), e.g. the one
  msvc-wine installs.
- `cmake/clang-msvc-sdk.cmake` - the same cross build with clang's GNU-style driver (`clang++
  --target=x86_64-pc-windows-msvc`, lld, llvm-ar) instead of clang-cl: the MSVC ABI and runtime as there, `MSVC_ROOT`
  by `-Xmicrosoft-windows-sys-root` (the driver's `/winsysroot`), `-std=c++23`. For an IDE whose code model
  is libclang reading `compile_commands.json` (KDevelop), which takes GNU-style options only; the `kdev-*` presets use
  it, the other presets and every release clang-cl.
- `cmake/get_cpm.cmake` - CPM, which brings stralgo and small_vectors (pinned to their commits).
- `cmake/edplugin_common.cmake` - the build steps the plugins share, as functions (see below), with
  `cmake/version.cmake` and `cmake/pin.cmake` they run at every build.
- The target `edplugin_common::edplugin_common`: a STATIC library (the code of log, paths, file, memory, pe, shader
  names, settings_doc, ini, guard, hook and chain) with the headers above, compiled as C++23 with `UNICODE`, `_UNICODE`, `NOMINMAX` and `WIN32_LEAN_AND_MEAN` (PUBLIC, so the plugin's code too), linked
  with stralgo; built with the MSVC runtime the plugin sets.
- `tools/wine-common.sh` - the common part of the plugins' `tools/test.sh` (wine prefix, run directory, a time
  limit on every wine run).
- `tools/context_probe/` - `context_probe.exe`, a diagnosis run by hand on Windows, without the game (a player's
  computer, a VM): on each hardware adapter and on the default device (no adapter given), where the D3D11 runtime keeps
  the method tables of a device and of its immediate context, which of the context's slots the runtime rewrites
  (multithread protection, `ClearState`), which entries lie outside the system d3d11 and where their trampolines lead
  (`module+offset`), which entries in the system d3d11 start with a jump out of it (an inline hook), the entries past
  the named methods counted by the module they lead to (the system's, the driver store's, another in the Windows
  folder, one outside it, none), and the process's modules outside the Windows folder (what was injected into it; a
  graphics driver's driver store is inside it). Then, on each device, the library's context hooks (`hooks.cc`): plugin A
  and plugin B attach by the rule (on Windows A makes a table of its own, B recognizes it by its mark and hooks in it in
  place), plugin C asks for a table of its own over them; each hooks `Draw`, `IASetPrimitiveTopology`, `ClearState`
  and `Flush`, counts its calls and calls its original; the calls go through `ID3D11DeviceContext4` (a `Draw` of
  no vertices), with the multithread protection on, off and after `ClearState` and `Flush`, each step followed by
  the watches' classes, the runtime's entries the stubs followed and the protection the library tells (the context
  keeps the tables and references to the end of the process, contract c). Then, on each device with no flags, a flip
  model swap chain (BGRA8, two buffers) on a window that is never shown, four `Present(0, 0)` without drawing, a
  `ResizeBuffers` and one `Present` more, each on a thread of its own with a time limit: after the swap chain is made,
  after the first and after the last `Present`, after `ResizeBuffers` and after the `Present` that follows it the swap
  chain's table (entries outside the system dxgi, entries in it that start with a jump out of it), the slots of the
  device's and the context's tables changed since before the swap chain with where they lead (the context's table the
  object points at and the one it came with), and the modules outside the Windows folder that came, for an overlay that
  hooks only at the swap chain or at `Present`; after the last `Present` the device's and the context's tables told
  again (an inline hook in the system d3d11 too) and the plugins' hooks looked at and called again. Every table is read
  as a copy, under `__try`; a fault in the plugins' hooks ends only their step (said in the result, no hooks on the
  devices after it). It is not the game: an overlay that hooks only a game it knows (by its exe) is not seen. The lines
  go to `context_probe-<date>-<time>.txt` beside it (a file of its own for each run); a player's instructions in
  `README.txt` there. The decoding of jumps, the methods' names by slot and
  the comparison of two reads (`jump.h`, `slots.h`) need no Windows header. Built at the top level with
  `EDPLUGIN_COMMON_CONTEXT_PROBE` (ON by default; its test with `EDPLUGIN_COMMON_TESTS` too);
  `tools/context_probe/package.sh <build directory> <output directory>` makes the zip a player gets (the exe, its
  `README.txt`, its sources, the library's sources it uses, `LICENSE`, `SOURCE.txt` with the library's commit and
  `SHA256SUMS.txt`) and prints the zip's checksum, to be posted apart from the zip; it stops at a build other than
  Release, at `EDPLUGIN_COMMON_UBSAN_TRAP`, at changes not committed and at an exe older than a source.
- `tools/event_report/` - `event_report.exe`, run by hand on a player's Windows: the game's and its launcher's crashes
  and the game's hangs (the Application log: Application Error 1000, Application Hang 1002, .NET Runtime 1026, Windows Error
  Reporting 1001; its LiveKernelEvent of graphics codes with the graphics, others apart, one noted twice within 2 s
  counted once), graphics driver resets and errors, restarts without a clean shutdown and blue screens (the System
  log queried by provider: Display 4101, nvlddmkm and amdkmdag errors and warnings, Microsoft-Windows-Kernel-Power 41,
  Microsoft-Windows-WER-SystemErrorReporting 1001, EventLog 6008; Intel's igfx* from a second reading of its errors
  and warnings, kept by the prefix), Windows Error Reporting's saved reports of the game (found by AppPath's exe,
  AppName or Sig[0]) and LiveKernelEvent in the machine's and the user's ReportArchive and ReportQueue (folders
  `AppCrash_`, `AppHang_`, `Kernel_` only; linked to Application Error 1000 by ReportIdentifier or
  IntegratorReportIdentifier; the files loaded into the game, as graphics drivers, the game's folder and elsewhere) and
  the crash dump folders (names, sizes and dates), over the last 30 days (folders and dumps by their time too). Only
  the fields `report.h` keeps reach the text, no path; the values read are cleaned before the text is made of them
  (the program's own words are not): user names (also the full name and the profile folder's short name), computer and
  domain names, e-mail addresses and SIDs (`S-1-5-21-`, `S-1-12-1-`) are replaced; control characters and the marks
  that turn the direction of text escaped. The text goes to a new `event_report-<date>-<time>.txt` beside it (`-2`
  and on for another run in the same second; never one replaced) and to the console; a player's instructions in
  `README.txt` there. From a command prompt, `--around <UTC date or time>` (`2026-11-19` the whole day,
  `2026-11-19T16:06` or with a space: `--minutes` either side, 60 unless given, 1..1440; at most 16) keeps only the
  log entries, saved reports and dumps of those times (`keep_around`, `report.h`), said in the head and in an empty
  section; a word not understood ends with 1 and the usage, before anything is read. `wevtapi.dll` is delay-loaded,
  after `SetDefaultDllDirectories` (System32 only). The part without Windows (`report.h`, `report.cc`) is checked natively (`test/native`) and under wine with Windows'
  comparison of names (`event_report_test.exe`), with a whole report compared byte for byte (`report_golden.h`);
  `event_report_test.exe` checks the result file's names too (`result_file.cc`: three runs in one second);
  `test/event_report_test.sh` runs the program twice under wine in a prefix of its own with saved reports (the user's
  and the machine's, one of another kind, one not UTF-16) and a dump put there and checks both its results whole,
  then `--around` with the dump's day and another, and a date that does not exist. Built at the
  top level with `EDPLUGIN_COMMON_EVENT_REPORT` (ON by default; its test with
  `EDPLUGIN_COMMON_TESTS` too); `tools/event_report/package.sh <build directory> <output directory>` makes the zip a
  player gets, as context_probe's.

## Use in a plugin

The plugin keeps a clone of the library in its own directory, ignored by its git (`.gitignore`), on the commit the
plugin pins in its file `edplugin_common.commit` (a full commit hash):

```sh
git clone https://github.com/arturbac/edplugin_common.git edplugin_common
git -C edplugin_common checkout $(cat edplugin_common.commit)
```

and in its `CMakeLists.txt`, before `project()`, a check that the clone is there (the toolchain is in it), then:

```cmake
add_subdirectory(edplugin_common)
target_link_libraries(<plugin> PRIVATE edplugin_common::edplugin_common)
edplugin_common_version(<PLUGIN>_VERSION <plugin>_version.h <plugin>)
edplugin_common_pin(<plugin>)
```

The plugin's presets take the toolchain from the clone: `"toolchainFile":
"${sourceDir}/edplugin_common/cmake/clang-cl-msvc-sdk.cmake"`, and for the IDE's `kdev-*` presets
`.../clang-msvc-sdk.cmake`. Options in MSVC's form stay out of the plugin's own CMake: warnings by
`edplugin_common_warnings`, linker options with CMake's `LINKER:` prefix (`LINKER:/DEBUG:FULL`, `LINKER:/EXPORT:...`),
which both drivers take. The library's tests build only when it is the top-level project.

- `edplugin_common_version(<macro> <header> <target>...)`: the header `<build>/gen/<header>` with the macro defined
  to the plugin's version, forced into the C++ sources of each target (not into MASM sources): `vX.Y.Z` for a
  release (HEAD on its tag, no uncommitted changes), else `<the last tag>-<short hash>`, with `-dirty` for
  uncommitted changes; `dev` outside git. `<macro>_COMMIT` is HEAD's full commit (`unknown` outside git),
  `<macro>_BUILD_TIME` the build's time in UTC (`2026-11-24T23:12:05Z`): `SOURCE_DATE_EPOCH` from the build's
  environment when set (seconds within 32 bits, else ignored), else HEAD's commit time, with `-dirty` as the version has it (`unknown` outside git
  without `SOURCE_DATE_EPOCH`); never the clock. In a
  Windows build each exe or dll target also gets a version resource (`<build>/gen/<target>_version.rc`, a source of the target), which Windows
  shows in a file's properties and in its crash reports: `FileVersion` and `ProductVersion` the version text,
  `InternalName` and `FileDescription` the target, `OriginalFilename` its file, `ProductName` the project; the
  numbers X,Y,Z,K are the last tag's with K the commits after it (0 on the tag; `0,0,0,K` before the first tag or,
  with a warning, after a tag not `vX.Y.Z`; `0,0,0,0` outside git; a shallow clone without the tag counts as before
  the first tag); the flag PRERELEASE when that tag is a pre-release (`vX.Y.Z-beta.N`), PRIVATEBUILD (with
  `PrivateBuild` the version text) for any build that is not a release. The header and each resource are written
  only when they change. Such a target also links reproducibly: its time stamp is the build's time above (without
  `-dirty`; lld-link's `/timestamp` from `<build>/gen/<header stem>_link.rsp`, which version.cmake writes and the
  link depends on; `/Brepro`, a hash, when there is no time), its PDB's id a hash of the PDB, and the PDB is named
  without a path (`/pdbaltpath:%_PDB%`). The same code and options give the same file in the same build directory
  (another directory gives another one: the objects' paths are in the PDB).
- `edplugin_common_warnings(<target>...)`: the warning level of the C++ sources of each target: `/W4` with clang-cl,
  the same `-Wall -Wextra` with the GNU-style driver.
- `edplugin_common_pin(<target>...)`: at every build, the clone against `edplugin_common.commit`, and the header
  `<build>/gen/edplugin_common_commit.h` with `EDPLUGIN_COMMON_COMMIT`: the clone's short hash, `-dirty` for
  uncommitted changes in it, ` (pinned <short hash>)` when it is not on the pinned commit, `unknown` when there is
  no `edplugin_common.commit` or git cannot read the clone. A plugin logs it at its start in a line of its own.
  Another commit, changes in the clone, a pinned commit missing from the clone (fetch it), no pin file or no git are
  a warning in the build's output; in a release build of the plugin (HEAD on a tag `vX.Y.Z`, a pre-release tag
  `vX.Y.Z-beta.N` too, no uncommitted changes) they are an error. git's own message is shown: a clone owned by
  another account may need git's `safe.directory`.
- `edplugin_common_fxc(<out> <profile> <entry> <variable> <source> [fxc options...])`: a header with the shader
  compiled by the Windows SDK's `fxc.exe` under wine; wine and fxc are looked for at the first call (cache
  `EDPLUGIN_COMMON_WINE`, `EDPLUGIN_COMMON_FXC`: the newest fxc in `MSVC_ROOT` unless set).
- `edplugin_common_proxy_exports(<tag> <list> [WRAP <name>...])`: the exports of a dll that stands in for a system dll
  (a `d3d11.dll` proxy: tag `d3d11`), made at configure time from `<list>`, a text file relative to the calling
  directory: one export name a line, `#` starts a comment anywhere in a line, spaces and empty lines are ignored. In
  `<build>/gen`:
  - `<tag>_proxy.def`: each name not in `WRAP` exported as `<tag>_proxy_thunk_<i>`, each `WRAP` name as
    `<tag>_proxy_impl_<name>`; no ordinals (the linker numbers the names in alphabetical order).
  - `<tag>_proxy_thunks.asm` (MASM): the slot table `<tag>_proxy_real_procs`, one pointer for each name not in `WRAP`;
    `<tag>_proxy_thunk_<i>`, one indirect `jmp` through slot `i` (no prologue: the caller's frame and arguments reach
    the target unchanged, whatever its signature), where `i` counts the names not in `WRAP` in the list's order; and
    `<tag>_proxy_unresolved`, which returns 0.
  - `<tag>_proxy.h`: `<tag>_proxy_names` (the names of the slots, in their order), `<tag>_proxy_count`, and the C
    declarations of `<tag>_proxy_real_procs` and `<tag>_proxy_unresolved`.

  The plugin adds `${<tag>_PROXY_ASM}` and `${<tag>_PROXY_DEF}` to its dll's sources (`<tag>_PROXY_HEADER` names the
  header; the `gen` directory is on the include path through `edplugin_common_pin`), fills every slot before the first
  call (`GetProcAddress` on the next module by the name in `<tag>_proxy_names`, `<tag>_proxy_unresolved` where it has
  none) and declares and defines `extern "C"` `<tag>_proxy_impl_<name>` with that function's signature for each `WRAP`
  name. A file is rewritten only when its text changes, so an unchanged list rebuilds nothing; a change of the list,
  or a deleted output, configures again. Errors, with the list's line where there is one, and nothing written: a tag
  or name that is not a C identifier (decorated names with `@` or `?` are not supported; a `.def` keyword such as
  `DATA`, `PRIVATE` or `NONAME` is not refused and does not belong in the list), a name twice, a `WRAP` name
  twice or not in the list, a list without names or with every name in `WRAP`, `;`, `[`, `]` or a byte outside ASCII
  in the list outside comments (comments are taken out first, so they may hold any text). The output follows the
  layout of the export thunks of the
  [EDVR unofficial patch](https://github.com/characterecho-sean/edvr-unofficial-patch) (MIT); no code of it is here.
- The plugin sets the MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY`, static `/MT`) and the debug information format
  before `add_subdirectory`; the library sets them itself only as the top-level project.
- CPM comes with the library (`cpmaddpackage` is there after `add_subdirectory`); stralgo and small_vectors are
  pinned in the library only. CPM tells packages apart by name only: the first `cpmaddpackage` of a package wins, a
  later one with another `GIT_TAG` is silently ignored. Without the network: as in "Build and test" below.
- A plugin's `tools/test.sh` sources `edplugin_common/tools/wine-common.sh`; its functions and their exit codes are
  described at the top of that file. Without the clone the plugin's test stops with the clone command.
- To another pinned commit: `git -C edplugin_common fetch origin` (from where the clone came), then
  `git -C edplugin_common checkout $(cat edplugin_common.commit)`. A build directory made before the toolchain moved
  into the clone keeps the old path: configure it again with `--fresh`.
- Push order: the library's commit is on GitHub before a plugin whose `edplugin_common.commit` names it; a release's
  tag `vX.Y.Z` is pushed by itself (a push of the branch does not take it).
- After a test one wineserver stays 600 s for the next one; `wineserver -k` with the same `WINEPREFIX` ends it.

## Versions

The version is in `project()` in `CMakeLists.txt`; a release is an annotated tag `vX.Y.Z` on the release's commit,
with its notes in `CHANGELOG.md`. A plugin pins a full commit (`edplugin_common.commit`), not a version: the tag only
names the commit, and the pin check compares hashes. A published tag is not moved; a correction is a new version.
The numbers count the library's releases and are not semantic versioning: what a plugin builds with is its pinned
commit, so a change of the API can come in a 1.x release, and `CHANGELOG.md` always describes it.

## Build and test

Requirements: CMake 3.29 or later, Ninja, clang-cl (the `kdev-*` presets: clang, clang++, llvm-ar, llvm-ranlib),
lld-link, llvm-lib, llvm-rc, llvm-mt, llvm-ml64, MSVC and the Windows SDK in `MSVC_ROOT`, git (the pin check at every
build), wine for the tests.

```sh
export MSVC_ROOT=<MSVC and Windows SDK directory>
cmake --preset edplugin_common-release
cmake --build build/edplugin_common-release
EDPLUGIN_COMMON_WINEPREFIX=<a wine prefix> tools/test.sh build/edplugin_common-release <scratch directory>
```

Without the network: `CPM_SOURCE_CACHE=<a filled CPM cache>` and `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` at configure.

`tools/test.sh` runs `test/pin_test.sh` (`cmake/pin.cmake` on a library and a plugin repository it makes, with
git and without wine), `test/version_test.sh` (`cmake/version.cmake` on a plugin repository it makes: the
header and the version resource of a dll and an exe for each case, and the resource linked into the build's
`test_exports.dll` by `llvm-readobj` and `strings`, without wine), `test/reproducible_test.sh` (`test/reproducible`, a
dll built as a plugin's with the build directory's toolchain, built twice, the second time from clean: the same file,
the commit's time as its time stamp, the PDB named without a path, and relinked with `SOURCE_DATE_EPOCH`'s time; without
wine), `test/proxy_exports_test.sh` (`edplugin_common_proxy_exports` by `cmake -P` on lists it makes,
without wine; the expected files in `test/proxy_exports/`) and `edplugin_common_test.exe` (a plain program, without
d3d11 or dxgi; with the log's checks, among them 4 threads writing batches at once, `log_flush` and a lost device's
line once a process without a device, the ini's files (made, completed
by their line ends, UTF-16 and a file that cannot be read left alone, a write that fails part way taken away or cut
back, the watch's looks), `pe_image` with its exports and
imports on every module of the process and on an image made in memory, whole and out of shape one way at a time, and
the guard's: faults and C++ exceptions, locks let go (also through a call by pointer, let go out of order and under a
nested guard), the codes that are no fault, `guard_t`'s limit and handler, a fault in another module, a thread
started again, the log batch ended by a fault and by a C++ exception, a fault in the log's own write last, and the guard's cost on
the CPU printed; the stack overflows are skipped under wine, which ends such a thread instead of raising the
exception, so they are checked on Windows only) and, when built,
`context_probe_test.exe` (the following of entries through trampolines it writes itself, without d3d11, the read under
`__try` on a page that cannot be read, and the hidden window, told as skipped under a wine with no display; run when the
build's `CMakeCache.txt` has `EDPLUGIN_COMMON_CONTEXT_PROBE` and `EDPLUGIN_COMMON_TESTS` on), and `event_report_test.exe`
with `test/event_report_test.sh` (when it has `EDPLUGIN_COMMON_EVENT_REPORT` and `EDPLUGIN_COMMON_TESTS` on; the latter
in a wine prefix of its own) under wine, each in a new directory under the scratch directory, and prints `PASSED` or
`FAILED`.
One wine run is limited to `WINE_COMMON_TIMEOUT` seconds (600 unless set). The CMake option
`EDPLUGIN_COMMON_TESTS` (ON by default) turns the test program off.

Sanitizers, in test builds only (a player gets the release build, without them):

- `EDPLUGIN_COMMON_UBSAN_TRAP=ON` (the preset `edplugin_common-ubsan`): UndefinedBehaviorSanitizer in trap mode, no
  runtime, for the library's and every linking target's C++ sources (a plugin sets the option at its configure);
  they also get the definition `EDPLUGIN_COMMON_UBSAN_TRAP=1`. Undefined behaviour ends the program with
  `0xC000001D` (an illegal instruction), under a fault handler of the library too (`fault_action` lets the trap go
  on, so the test ends instead of logging a fault). `tools/test.sh` runs the build as any other.
- Not for this target under wine: AddressSanitizer (the runtime MSVC ships stops at the start under wine), UBSan
  with its runtime (MSVC's does not link without AddressSanitizer's), ThreadSanitizer (clang-cl has none for
  Windows).
- `tools/test-native.sh <scratch directory>`: the headers without a Windows header (`test/native`:
  `memory/pe_range.h`, `memory/pass_modules.h`'s `next_pass_name`, `shader/shader.h`, `ini/ini_text.h`,
  `ini/ini_table.h` and `ini/live.h` - the latter with a thread that keeps a reference to the start-only fields through 200 publications -, `tools/context_probe/jump.h`
  and `slots.h`)
  built natively with clang++ and its compiler-rt sanitizers, with `-fsanitize=address,undefined` and with
  `-fsanitize=thread`, and run; no wine and no MSVC. Each configure, build and run is limited to `NATIVE_TIMEOUT`
  seconds (600 unless set); a run stopped by the limit is said with its exit code.
