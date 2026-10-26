# PPCabbyCodes — project guide

A client-side mod for **Papers, Please**, Steam app 239030: game 1.4.11.124, Steam build **10817815**, Unity
2020.3.34f1 with IL2CPP (metadata v27), threaded rendering. One mod, two builds of the same code:

- **Linux**, the native build (`GameAssembly.so`, OpenGL 4.5 core): one library,
  `PapersPlease_Data/Plugins/libPPCabbyCodes.so`, which Unity loads by itself.
- **Windows** (depot 239031: `GameAssembly.dll`, Direct3D 11), cross-compiled with mingw-w64: `hid.dll` beside
  `PapersPlease.exe`, which the game loads as an import of `UnityPlayer.dll` (the user, 2026-10-26: "I want this mod
  to work for windows as well"), and which works beside the game's mod loaders, MelonLoader and BepInEx (the user,
  the same day: "is there a way we can make it compatible?").

While the game's pause menu is up it draws a Dear ImGui panel with three switches: **Lock Inspector Materials**,
**Cross-Off Criminals** and **Pause game after failure**. During a held mistake it also draws a **Continue** box.
`README.md` is the user doc (`README_BBCode.txt` the same for Nexus); this file records what was reverse-engineered
and how the mod is put together.

The user's request (2026-10-25), which is the spec:

> When the game is paused I want an imgui cheat menu available. For starter options I want: 1) Lock Inspector
> Materials - which should freeze the book and the newspaper to their placement on the table. It should also
> remember the book and newspaper's currently tabbed to pages, but not force them to stay on that page. When
> starting a new day, the book and newpaper should default to their locked position and page. Attempts to drag the
> book or newspaper will not move them. 2) Cross-Off Criminals - For the wanted criminals listed on the newspaper,
> remove pictures of the people who have already been found. 3) Pause game after failure - When failing to
> diagnose an entrant's papers, don't take the papers away immediately and pause the timer. This will allow the
> user to look over the materials again to understand what went wrong. Add a small continue button on the screen
> when in this mode to allow the user to unpause, which will do whatever behavior the game intended to do after
> that failure (endless modes might choose to end the day for example). This mode should still allow for the
> citation ticket to appear so that the user knows what was flagged wrong.

"Book" is the rulebook (PaperDef `Rules`), "newspaper" the bulletin (`Bulletin`). "Found" criminals are the
day's wanted criminals who have already come through the booth (`Criminal.used`), apart from the one at the
window now.

Unlike the siblings (Windows games under Proton, DLL proxies), this mod's engine code is its own, apart from the
Windows overlay's swap chain adoption, which is RE2/RE4CabbyCodes' design (their `overlay_d3d.cpp`) for D3D11 alone.
The conventions follow theirs: the panel, the ini, the Makefile's targets, the docs.

## Source layout

`src/` is the mod, shared by both builds. `src/linux/` and `src/win/` are each build's own: how it gets into the
game (`main.cpp`), its renderer (`overlay_gl.cpp`, `overlay_d3d11.cpp`), its hooks' memory and import hooks
(`hook_linux.cpp`, `hook_win.cpp`) and `platform.cpp`, which implements `src/platform.h`: thread ids, the wall
clock, files by UTF-8 path (UTF-16 on Windows; `replace_file` moves over an existing file, which a plain rename
refuses on Windows), a library's exports, and `guarded`, the mod's frame's catch. `src/win/proxy.cpp`,
`hid.def` and `resource.rc` make the Windows build `hid.dll`. The shared files call none of the platforms'
APIs.

## Build & deploy

```sh
make            # -> build/libPPCabbyCodes.so and build/win/hid.dll (config.mk: GAME_DIR, WIN_GAME_DIR; gitignored)
make linux      # (or make windows) one of them
make test       # builds and runs the tests (tests/build/): the Linux ones, then the Windows .exe under Wine
make install    # atomic copies: $GAME_DIR/PapersPlease_Data/Plugins/libPPCabbyCodes.so and, when WIN_GAME_DIR is
                # set, $WIN_GAME_DIR/hid.dll (never over a hid.dll that is not this mod's; an older build's
                # version.dll there is removed)
make uninstall  # removes them (the ini and log stay in the game folder)
make dist       # dist/PPCabbyCodes/: Linux/libPPCabbyCodes.so, Windows/hid.dll, README.md, LICENSE, INSTALL.txt
make package    # dist/PPCabbyCodes_v<version>.zip (no VERSION file: 0.0.0)
```

**Always install after a change.** Once a change builds and its checks pass, run `make install` yourself, then
confirm with `cmp build/libPPCabbyCodes.so "$GAME_DIR/PapersPlease_Data/Plugins/libPPCabbyCodes.so"` (and
`cmp build/win/hid.dll "$WIN_GAME_DIR/hid.dll"`) and say in the reply that it is installed. The install is
a rename over the old file, so it is safe while the game runs: a running game keeps the copy it loaded until it
restarts, and `make install` notes that. Installing is not a release: the version stays as it is.

`WIN_GAME_DIR` is a copy of the Windows game, not a Steam install: the user has the Linux one. Steam's console
(`steam://open/console`) downloads the Windows depot beside it, leaving the Linux install alone:
`download_depot 239030 239031` (done 2026-10-26; it lands in
`~/.local/share/Steam/ubuntu12_32/steamapps/content/app_239030/depot_239031`, which is `WIN_GAME_DIR` here). The
user does that: it needs their Steam session.

The VERSION file is the user's: never create or change it, never run `make version`/`make rev`, never suggest a
bump. The panel's title shows the version (0.0.0 without a VERSION file).

Build flags that matter, Linux: `-static-libstdc++ -Wl,--exclude-libs,ALL` (Steam's runtime may carry an older
libstdc++; the folded-in runtime's symbols stay local), `-fvisibility=hidden` plus `src/linux/exports.map`
(`local: *`) so that the library exports nothing, and `-z defs` (every symbol resolved at link time). **Not**
`-static-libgcc`: GameAssembly.so throws its managed exceptions through the system's `libgcc_s.so.1` (it needs it,
so the game always has it loaded), and the mod's catch must work them with that same unwinder. With a private copy
the game's unwinding called the mod's personality, whose `_Unwind_SetGR` (the private copy's) found its register
table never set up and aborted the game (the harness's `throw`, 2026-10-26: SIGABRT in `_Unwind_SetGR.cold`; this
was in every build until then). `test_load` checks the library reaches the process's `_Unwind_SetGR`. No X11 or GL
headers are needed: `linux/overlay_gl.cpp` declares the few GLX/GL entry points it uses.

Windows: `-static -static-libgcc -static-libstdc++` (the C++ runtime and winpthreads folded in; the C runtime is
Windows' UCRT, `api-ms-win-crt-*`, part of Windows 10 and 11, which Steam requires), `-s` (the static runtime's
debug sections made it 14 MB; it is 2 MB), and `src/win/hid.def`, so that it exports exactly hid.dll's 47
functions. It imports kernel32 and user32 alone: the game loads it at start-up, where a missing DLL stops the game,
so d3d11, dxgi and d3dcompiler are used only through what the game made or loaded (`D3DCompile` is a stand-in that
loads d3dcompiler_47 when ImGui's DX11 backend first asks), and ImGui's shell and clipboard functions are left out
(`IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS`, `IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS`).
`__USE_MINGW_ANSI_STDIO=1` gives it mingw's C99 printf (msvcrt's knows no `%zu`; `logf` is checked as
`gnu_printf` there). `near` is an empty macro in windef.h: never a name.

### Tests (offline)

| test | what it checks |
|---|---|
| `test_hook` | the inline hook engine on the prologue shapes GameAssembly.so and libGLX.so use (endbr64 + push, a RIP-relative compare first, a short jcc within the stolen bytes, a call first) and, on Windows, MSVC's (a register saved in the caller's home space, a push, a stack allocation): each hooked, called through the detour and through the trampoline, the relocated operand and branch still right; a function too short to patch is refused. Both builds |
| `test_load` | the built library loaded by a program that is not the game: it loads, writes no log, exports nothing, and unwinds with the process's libgcc_s (no `-static-libgcc`: see the build flags) |
| `test_config` | the ini, in a folder whose name is not ASCII: defaults written when missing, read back, the switches and new keys (the lock's) saved with every other line, comment and the order kept (the save replaces the file); key names to Unity KeyCodes. Both builds |
| `test_guard` | `platform::guarded`, the mod's frame's catch: a call that returns; the game's exception, thrown as its code throws it (a C++ throw; on Windows `RaiseException(0xE06D7363)`, MSVC's), from below a frame of the mod's with a destructor; the mod's own C++ exception; a call after those; a guard inside a guard. Both builds |
| `test_proxy` | the Windows build loaded by a program that is not the game: it loads, writes no log, exports exactly hid.dll's 47 functions, they answer as Windows' own (the HID class GUID; a call on no device fails alike), an export Windows' own lacks (Windows 11's, under Wine) fails with HIDP_STATUS_NOT_IMPLEMENTED, and it unloads. Windows only |
| `test_slot` | `hook::iat_slot`, `write_slot` and `slot_in_front` on the program's own GetProcAddress import, against a hook shaped like MelonLoader's and Doorstop's (il2cpp_init its own, the rest Windows'): a loader in the slot first, the mod goes in front and its lookups go through the loader; a loader that overwrites the mod after it, the mod taken back in front. The slot's page is read-only: reading it with a compare-exchange faulted (it writes even when it fails). Windows only |
| `test_panel` | the panel (`panel.o` with ImGui's core, no renderer) driven by mouse events: an open panel's switch takes a click; the title bar's arrow folds it to its title; the fold goes into the ImGui ini, and a new context that reads it comes up folded; the arrow opens it again; a double-click on the title folds it. Built against the old `NoCollapse` flag it fails the five fold checks |

The Windows tests (`tests/build/win/*.exe`, built with mingw-w64) run under the system's Wine in a prefix of their
own (`tests/build/win/prefix`, its HOME `tests/build/win/home`), with `DISPLAY` and `WAYLAND_DISPLAY` unset so
nothing can reach the desktop (prefix creation shows a window otherwise), no menu entries or browser, and
`hid=n,b` (Wine loads its own hid.dll in place of a file otherwise). The user's shell has `DISPLAY` set:
run no Wine outside the nested KWin or without unsetting it.

### Testing in the real game, headless

The user plays the game. **Never drive the game in their session, never send it input, never kill it.** A copy of
the game run by `tools/headless.sh` is the only one the mod is ever tested in:

```sh
tools/headless.sh start 1920 1080        # nested KWin + the game; returns once the harness answers
WAIT=6000 tools/headless.sh do 'screen TitleScreen' 'ms 30000' ...   # harness commands (src/testkit.h)
tools/headless.sh shot NAME              # $XDG_RUNTIME_DIR/ppcc-headless/NAME.png
tools/headless.sh log                    # the run's PPCabbyCodes.log
tools/headless.sh stop                   # kills only the game it started (pid + PPCC_TEST_DIR check), then KWin
```

- **The display.** The script runs `kwin_wayland --virtual --xwayland --socket ppcc-wayland`, a compositor of our
  own with no output, so nothing shows on the desktop. The user's own `kwin_wayland` (wayland-0/1) is not ours:
  leave it alone. The game sizes its window to 1140x640 (twice its 570x320 art) whatever `-screen-width` says, so
  the recipes' coordinates are for 1140x640.
- **KWin's own bus and config.** The nested KWin runs under `dbus-run-session` on a bus of its own (`$RUN/bus.conf`:
  no service files, so nothing is D-Bus-activated beside it) with `XDG_CONFIG_HOME=$RUN/kwin-config`, and
  `kwin.pid` is KWin's own pid. Until 2026-10-25 it shared the user's session bus: it joined their kglobalaccel
  (owned by their KWin) as a second `kwin`, and each time it exited it set every KWin shortcut inactive, so Alt+Tab,
  Alt+F4, Meta+D and the rest stopped working on their desktop. It also wrote its `Virtual-0` output into
  `~/.config/kwinoutputconfig.json`. Keep both. The game uses no D-Bus at all (none of its binaries reference it).
- **Isolation.** The game runs with `PPCC_TEST_DIR` (the mod's log and ini go there), `XDG_CONFIG_HOME` (Unity's
  prefs) and `XDG_DATA_HOME` (the saves, copied from `~/.local/share/3909/PapersPlease` on the first start) all in
  the run directory, and with `-logFile` there too.
- **Steam.** Started outside Steam, the game calls `SteamAPI.RestartAppIfNecessary(239030)`, which asks the
  running Steam client to start the game itself (it would come up on the user's desktop). `-nosteam` does not stop
  this. The testkit hooks
  `Steamworks.SteamAPI.RestartAppIfNecessary` and `SteamAPI.Init` to return false (only under `PPCC_TEST_DIR`),
  and the game carries on without Steam, so it has no Steam session and no Cloud sync.
- **Never `pkill -x PapersPlease`**: it is the user's game too. A stop that once did so may have ended the user's
  session (2026-10-25, about 18:45). `stop` checks the pid's environ for this run's `PPCC_TEST_DIR`, sends SIGTERM,
  then SIGKILL after 3 s: Unity can ignore SIGTERM once its display is gone, and 22 orphaned test games once piled
  up that way. After a session, check for leftovers: every `PapersPlease` process whose environ has
  `PPCC_TEST_DIR=` is ours. (`pkill -f`/`pgrep -f` with the pattern in the command line also matches your own
  shell.)
- **Timing.** The title screen ignores clicks for about 30 s after it appears (`'screen TitleScreen' 'ms 30000'`).
  A headless game may run uncapped, so wait in milliseconds (`ms`), not frames. Commands go in batches numbered
  `#seq N`; the harness acks N when the batch is done, and `$RUN/abort` drops what is queued.
- **The Windows build:** `WIN=1 tools/headless.sh ...` runs `$WIN_GAME_DIR/PapersPlease.exe` (with `hid.dll`
  installed there) under the system's Wine in the same kind of nested KWin (socket `ppcc-wayland-win`, run directory
  `$XDG_RUNTIME_DIR/ppcc-headless-win`). The prefix is the run's own (`$RUN/pfx`, made at the first start with no
  display; Wine's HOME and XDG folders are `$RUN/home`, so nothing of Wine's lands in the user's home), with
  `hid,version,winhttp=n,b` (the mod's and the mod loaders' DLLs from the game's folder) and no menu builder or
  browser (a `steam://` link must not reach the desktop's Steam). `mscoree` is turned off only while the prefix is
  made (no Wine Mono): .NET loads assemblies through it, and with it off MelonLoader's Cpp2IL failed with a
  FileNotFoundException for a plugin that was there ("mscoree.dll not found, IL-only binary ... cannot be loaded").
  The saves are
  copied into the prefix's `%APPDATA%\3909\PapersPlease` (the user's Linux saves load in the Windows game);
  `PPCC_TEST_DIR` is the run directory as Wine names it (`Z:\run\user\1000\ppcc-headless-win`), and `stop` ends the
  prefix's processes with `wineserver -k`, nothing else. Wine's own Direct3D (wined3d) draws by default;
  `DXVK=<folder of DXVK's x64 dxgi.dll and d3d11.dll>` (Proton Experimental's
  `files/lib/wine/dxvk/x86_64-windows`) puts DXVK in a prefix of its own, as under Proton (run directory
  `...-win-dxvk`; DXVK's logs and cache are off, or they land in the game's folder). The coordinates and the recipe
  below are the same; the game runs about as fast. Steam's client is never involved: plain Wine has no bridge to it,
  and the testkit's Steam hooks go in as on Linux. `PPCC_GAME_DIR` runs another copy of the game (one with a mod
  loader beside the mod, with `RUN` set to a run of its own), and `PPCC_TEST_LOADER=1` has the mod put a stand-in mod
  loader in UnityPlayer's GetProcAddress slot right after its own DllMain (below). Discord's game detection reads the process list: it sees a
  headless game (Linux's `papersplease` at least) as the user playing.
- **Into a day:** `'screen TitleScreen' 'ms 30000' 'click 568 490' 'screen LoadScreen' 'ms 2500' 'click 62 180'
  'ms 1500' 'click 568 410' 'screen NewsScreen' 'ms 3000' 'click 568 598' 'screen DayScreen' 'booth empty 120000'
  'ms 6000'`, then `key ESCAPE` pauses. That loads the load list's first entry, which was day 24 (1982.12.16,
  features 0x23fd: FILER, CRIMINALS, DENIAL_REASON) with the user's saves. The run's saves move on as tests play
  (ending a day saves the next), and after the day-25 test the same clicks loaded day 21 (0x23dd, no FILER). Check
  the day with `state`, and empty `$RUN/data/3909/PapersPlease` to start again from a fresh copy of the user's
  saves. To reach the next day quickly: `travelers N` (the day's minimum) and `clock S` (the booth clock jumps
  ahead). `crimused N` marks a wanted
  criminal as having come through, and `switch`, `request` and `panel` set the panel's state directly (`switch`
  sets the atomic only: the run's ini does not follow it). `continue` clicks the Continue button where the overlay
  last drew it. `state` logs the booth (day, features, state, clock, engine flags, criminals, every paper with its
  page, position, placement and state). `throw` has GameAssembly's own code raise a managed exception
  (`il2cpp_raise_exception`) inside the mod's frame: the guard must catch it, the log says "the mod's frame threw",
  and the game goes on; the mod's frame stops for good, so it is a run's last command (its batch is acked first).
- **By hand in a day** (screen = 2 x booth + (0, 206), the booth's 570x320 at 2x): the bulletin opens on the desk at
  screen 596,210, its next-page corner at 878,594 (three clicks reach the wanted criminals on day 24); the
  loudspeaker that calls the next entrant at 345,152; the stamp bar's tab at the right edge, 1128,312 (open:
  DENIED's head at 735,300, APPROVED's at 975,300, the visa box under it). A document is handed back by dragging it
  up past the counter's top edge into the entrant's window (`drag 185 490 190 300`); dropped on the counter it only
  lies there. Process an entrant wrongly from `state`'s "papers have errors" (deny a clean one) to see a hold.

## ⛔ Rules

- **Never change the game's files.** The mod is an extra library: in `Plugins` on Linux, `hid.dll` beside the exe
  on Windows; nothing is renamed or replaced. (An early build stood in for `libsteam_api.so`; `make install` puts the
  game's own back if it finds that layout.)
- **Never hard-code addresses or offsets.** Classes, fields and methods are found by name through the IL2CPP API
  (`pp::bind`, `game::start`), and Unity's input through `il2cpp_resolve_icall`. The RVAs in this file document
  build 10817815's GameAssembly.so only (GameAssembly.dll's differ; the Ghidra project is the Linux one's).
- **The game's objects only on the game's thread**: the tick (`HostUnity.Update`) and the hooks the game calls.
  The render thread draws ImGui from the published model and posts requests as atomic flags.
- **The user's saves**: `~/.local/share/3909/PapersPlease` (and Unity's prefs in
  `~/.config/unity3d/3909/PapersPlease/prefs`). A copy is in `.save-backup-2026-10-25/` (gitignored). Tests run on
  the harness's own copy only.
- The user commits every repo themselves: do not `git commit`/`push` unless asked.

## How the mod gets in: Linux (`linux/main.cpp`)

- Unity preloads every `.so` in `PapersPlease_Data/Plugins` at start-up on the main thread ("Preloaded
  'libPPCabbyCodes.so'" in Player.log). It loads `GameAssembly.so` and runs `il2cpp_init` **before** that, so at
  the library's constructor the runtime is up (`il2cpp_get_corlib()` is non-null) and none of the game's code has
  run: `game::start()` finds the classes and hooks them there and then, on the main thread.
- Fallbacks, for a Unity that loads in another order: if the runtime is not up, an inline hook on `il2cpp_init`
  starts the mod as it returns; if `GameAssembly.so` is not even loaded, `UnityPlayer.so`'s GOT slot for `dlsym`
  hands back the mod's `il2cpp_init` (everything else goes on to the real `dlsym` as a `musttail` call, so `dlsym`
  still sees UnityPlayer as its caller).
- The constructor does nothing unless the executable is `PapersPlease`, which keeps `test_load` and any other
  loader out. The log and ini live in the game folder, or in `$PPCC_TEST_DIR/` under the harness.
- `il2cpp_get_root_domain` is not exported by this build; the mod does not use it.
- Globals that the constructor's code reaches (the log's path and lock, the config, the overlay's ini path and
  input queue, the panel's model) are **function-local statics made on first use and never destroyed**. As plain
  globals in other files they were not yet constructed when the constructor ran (link order); this crashed at
  start-up on 2026-10-25. Never destroying them also keeps exit-time destructors away from the render thread,
  which may still be drawing.

## How the mod gets in: Windows (`win/main.cpp`, `win/proxy.cpp`)

- Unity's **Windows** player preloads no plugin: its UnityPlayer.dll has no "Preloaded" message nor the Linux
  player's "Unable to preload the following plugins", and a probe DLL in `PapersPlease_Data/Plugins` and in
  `Plugins/x86_64` was never loaded (2026-10-26), nor is there a `GfxPlugin` early load. So the mod comes in as a DLL
  the game imports at its start: one of UnityPlayer.dll's imports that is not among Windows' KnownDLLs, which
  Windows looks for beside the exe first. Those are ADVAPI32's neighbours bcrypt, CRYPT32, dwmapi, HID, OPENGL32,
  VERSION, WINHTTP and WINMM. The mod is **`hid.dll`** (UnityPlayer imports 14 of its functions, for controllers):
  - `version.dll` is MelonLoader's name, and MelonLoader 0.7.3 x64 is what this game's mods use (2026-10-26, Nexus:
    the Discrepancy And Story Assistant, the Kasane Teto pack of the Custom Traveler Engine, a GitHub framework;
    BepInEx 6: Always Have All Upgrades, a GitHub cheat). Its installer writes `version.dll` (over the mod's, when
    the mod had the name: the first Windows build did, for a few hours). `winhttp.dll` is BepInEx's Doorstop;
    MelonLoader can also be renamed to winmm, winhttp, dinput(8), dsound, d3d8-12, ddraw or msacm32.
  - hid.dll's exports are 47 plain named functions (no data, none by ordinal alone); bcrypt and CRYPT32 carry the
    process's cryptography, dwmapi has ordinal-only exports, OPENGL32 is the GL loader.
- **The proxy**: each export (`hid.def`) is a `jmp *slot(%rip)` thunk, so one thunk serves any signature; DllMain
  points the slots at System32's hid.dll, loaded by its full path (by name Windows hands back the mod). Every module
  of the process that imports hid.dll (the game, DirectInput, Steam's overlay) gets the mod and through it Windows'
  own, so the mod exports all of Windows': the 44 of every Windows since XP (Wine's and mingw's lists agree) and
  Windows 11's three of HID API version 2 (`HidP_GetButtonArray`, `HidP_SetButtonArray`,
  `HidP_UsageAndPageListDifference`, from Microsoft's hidpi.h list; `HidP_GetVersion` is header-only). One Windows'
  own lacks fails: FALSE and ERROR_PROC_NOT_FOUND for a HidD_ function, HIDP_STATUS_NOT_IMPLEMENTED for a HidP_ one.
  Unity's crash handler does not import hid.dll; any other program that loads the mod gets the forwarding alone.
- **DllMain** runs as the process starts, under the loader lock, before GameAssembly.dll is loaded: the forwarding,
  the log and ini (beside the exe, or `PPCC_TEST_DIR`), `overlay::install`, and a hook on UnityPlayer.dll's import
  slot for `KERNEL32!GetProcAddress`. UnityPlayer looks up every `il2cpp_*` function with it as it loads
  GameAssembly.dll, and `CreateDXGIFactory2` (1, plain) as it loads dxgi.dll. For `il2cpp_init` the hook hands back
  the mod's own, which calls the one it was given and starts the mod (`game::start`) on the main thread, out of the
  loader lock, before any of the game's code has run (as Linux's fallback does with dlsym); the factory functions
  go to the overlay. A runtime already up (a mod loaded late, not the way the game loads it) leaves the mod out.
- **The mod loaders want the same slot.** MelonLoader 0.7.3 (`ModuleSymbolRedirect`: PltHook on the module whose
  file name contains "UnityPlayer", from its DllMain) and Doorstop (`iat_hook` of UnityPlayer's
  `kernel32!GetProcAddress`, in its DllMain) each put a GetProcAddress there that answers `il2cpp_init` (MelonLoader
  `il2cpp_runtime_invoke` too) with its own and asks Windows' own for the rest, never what the slot held: whoever
  is last in the slot is the only one asked, and before 2026-10-26 the mod was such a hooker too. So the mod calls
  on to what it found in the slot (`hook::slot_in_front`), and takes the slot again as GameAssembly.dll loads (an
  `LdrRegisterDllNotification` callback): after every DllMain of the game's start, before UnityPlayer looks anything
  up in it. Whichever DllMain runs first, the mod is in front, wraps the loader's `il2cpp_init`, and the loader
  starts first. A re-entry guard sends a lookup that comes back to the mod on the same thread (a hooker behind it
  that calls on to what it found, the mod) to Windows' own. Seen 2026-10-26 under Wine, which ran both loaders'
  DllMains before the mod's: MelonLoader 0.7.3 (with .NET 6.0.36 in `dotnet/`) generated its assemblies and loaded
  a probe mod whose Harmony prefixes sit on `HostUnity.Update` and `DeskItem.updateCustomDrag`, both of which the mod
  hooks too: both hooks ran (the probe counted the frames and the drag, the lock still held the bulletin). BepInEx
  6.0.0-be.785 (the user's Iron Nest copy: `winhttp.dll`, `doorstop_config.ini`, `BepInEx/core`, `dotnet/`) reached
  "Chainloader startup complete". The other order (the mod's DllMain first, then a loader's: likely on Windows,
  whose order Wine does not show) by the harness's stand-in loader (`PPCC_TEST_LOADER`): the slot taken back as
  GameAssembly.dll loaded, the stand-in's `il2cpp_init` ran, then the mod's start. MelonLoader does its own start
  on the first `il2cpp_runtime_invoke`, after the mod's hooks are in; a mod's Harmony patch then sits on top of the
  mod's jump (Dobby relocates it).
- Under Wine or Proton a `hid.dll` beside the exe needs `WINEDLLOVERRIDES="hid=n,b"` (Wine's own builtin is taken
  first otherwise; with MelonLoader `hid,version`, with BepInEx `hid,winhttp`); the README gives Proton players the
  launch option. Linux players use the native build.
- IL2CPP's C signatures are the same on both; the calling conventions are each compiler's: Windows' x64 ABI passes
  arguments by position (`updateCustomDrag`'s dt in xmm1, `getImage`'s scale in xmm3, its ints on the stack) and
  returns `PointerState` (16 bytes) through a pointer the caller passes in rcx, which mingw does for the same
  declarations. The hooks moved 5 to 8 bytes of GameAssembly.dll's MSVC prologues each (2026-10-26).

## Threads

- **The game's main thread** runs `HostUnity.Update` (hooked): `game::tick` runs the testkit, samples Unity's
  input for the panel, the toggle key (only while paused), takes the panel's requests, runs `cheats::tick` and
  publishes the panel model. Its body runs inside `platform::guarded`: IL2CPP throws managed exceptions as C++
  exceptions, so a game call that throws stops the mod's tick for good (logged) and leaves the game's frame
  running. On Linux that is a `try/catch(...)` (with the game's own unwinder: no `-static-libgcc`). On Windows they
  are MSVC's (`0xE06D7363`), which mingw's catch does not see: an assembly guard whose unwind data names a handler of
  the mod's catches them and unwinds to itself (`RtlUnwindEx`; the game's frames run their destructors), with a
  mingw `catch` inside it for the mod's own. The mod's frames between are unwound without their destructors (only
  memory leaks: none holds a lock across a game call; keep it so). Both were seen to work on a real exception with
  `throw` (2026-10-26). The feature hooks are called by the game on this thread as well, since the Haxe game is
  single-threaded.
- **Unity's render thread** presents, and the panel is drawn there. Its frame is shared (`overlay.cpp`): it feeds
  the input samples, scales the UI to the window height (`h / 640`, clamped to 1..4: style sizes and
  `FontScaleDpi`) and draws `panel::draw()`; each platform's renderer catches the present and renders the frame.
- **Linux**: `glXSwapBuffers`; the hook (`linux/overlay_gl.cpp`) runs the panel's frame there, saves and restores
  the GL state ImGui's backend leaves changed (draw framebuffer 0, sRGB off, rasterizer discard off, colour mask,
  unpack parameters), and renders. The GLSL version is `#version 150` for the 3.2+ core context. The hook is in glvnd's `libGLX.so.0`, which `libGL.so.1`'s
  `glXSwapBuffers` forwards to (`libGL.so.1` is the fallback).
- **Windows** (`win/overlay_d3d11.cpp`): the game's DXGI factory comes back through the mod (above); its class
  vtable's QueryInterface, CreateSwapChain and CreateSwapChainForHwnd slots are hooked in place, and each swap chain
  made for the game's window (made by the main thread, or of class `UnityWndClass`) is given a vtable of its own: a
  copy of its class's (64 slots and the one before, where MSVC keeps type information) with the mod's Present,
  ResizeBuffers, Present1 and ResizeBuffers1, which call on through the class's vtable as it is at the time. Why
  not the class's vtable: on Windows the Steam overlay hooks a swap chain by patching the start of each function
  its slots point to, and cannot decode mingw's code (RE2CabbyCodes' players, 2026-09-25). Wrappers beside the exe
  (ReShade, Special K, OptiScaler) are known by their IIDs and never asked for higher interfaces (Special K's
  QueryInterface hands back without a reference). The panel draws with ImGui's DX11 backend into the back buffer
  (a view of buffer 0, made again after ResizeBuffers) inside a device-context state of its own
  (`SwapDeviceContextState`): Unity caches its context's state and would not set again what ImGui's backend changes
  (it clears three shader stages and the render targets); without D3D11.1 it saves the targets around. Seen
  2026-10-26 under Wine's own Direct3D and under DXVK: CreateDXGIFactory2 (32 slots), CreateSwapChainForHwnd (41),
  flip-discard, 2 buffers, format 28, resized to 1140x640 at once. The Steam overlay itself was not there (no Steam
  under the harness): that part rests on the siblings' experience.
- **Input.** Unity's own icalls (`Input.get_mousePosition_Injected`, `GetMouseButton`, `GetMouseButtonDown`,
  `get_mouseScrollDelta_Injected`, `GetKeyDownInt`, `Screen.get_width`/`get_height`), read on the main thread.
  While ImGui wants the mouse, `InputWrapper.GetPointerState` (hooked) reports the buttons up, so a click on the
  panel never reaches the desk. The game draws its own cursor under the panel, so over the panel ImGui draws one
  (`MouseDrawCursor`).
- **Panel requests**: Continue and Save (a switch changed; the tick copies the atomics into the settings and writes
  the ini). The switches themselves are atomics (`switches.h`) that the tick reads each frame.

## Hooking (`hook.cpp`, `linux/hook_linux.cpp`, `win/hook_win.cpp`)

x86-64 inline hooks: HDE64 (`contrib/hde`, from MinHook) gives instruction lengths. The trampoline relocates
RIP-relative operands, jcc, call and jmp, and is allocated within ±2 GB of the target, which keeps the 5-byte
`jmp rel32` patch: on Linux with `mmap` and `MAP_FIXED_NOREPLACE`, stepping 1 MB; on Windows in a free region found
with `VirtualQuery` at the 64 KB allocation granularity (as MinHook does), below the target first. `got_hook` (Linux)
patches `JUMP_SLOT`/`GLOB_DAT` entries and handles RELRO. On Windows `iat_slot` finds a module's import address
table slot by name (through its import name table), `write_slot` writes a pointer into a read-only page (an import
table's, a class's vtable), and `slot_in_front` puts a hook in front of whatever holds a slot, keeping it to call on
to. Read a slot with a plain atomic load: a compare-exchange writes even when it fails, and faults there. An IL2CPP method's code is `MethodInfo->methodPointer`, and its
C signature is `(this, args..., const MethodInfo*)`. A struct returned by value comes back in registers by the SysV
rules: `InputWrapper.PointerState` (16 bytes: x, y, z floats and four bools) returns in xmm0 and rax.

## What the game is (build 10817815)

The game is Haxe compiled to C# and then to IL2CPP. Classes keep their Haxe names and packages as namespaces
(`play.day.booth.Booth`), and nearly every method is virtual. The Haxe runtime's shapes:

- `Array` (global namespace): `length` @0x10, `__a` (object[]) @0x18. Elements of an IL2CPP array start at +0x20.
- `haxe.ds.List`: `h` @0x10, linked through `haxe.ds._List.ListNode` (`item` @0x10, `next` @0x18).
- Enums are objects: `haxe.lang.Enum._hx_index` @0x10. Constants are static fields of the enum class
  (`DeskItemState.NORMAL`, `EngineEvent.PROCESSING_FINISH`).
- Closures are classes of their own, so a method reached only through a closure has **no code xrefs**
  (`DeskItem.returnToDock`). To find who calls a virtual method, grep the decompiled code for its vtable slot name
  (`_53_setInnerImage`).

### The frame, the screens, the pause
- `HostUnity.Update` (RVA 0x1021650): the game's frame. `HostUnity.game` is the `Game`.
- `Game.pauseScreen` is non-null while the pause menu is up. `Game.gameScreen` is the current screen (`TitleScreen`,
  `LoadScreen`, `NewsScreen`, `DayScreen`, ...), and `Game.stater` is an `app.Stater` (`curState.name`).
- `play.screen.DayScreen.booth` is the day's `play.day.booth.Booth`.

### The booth
- `Booth`: `papers` (a List of `Paper`: everything on the desk and counter), `engine` (`BoothEngine`), `day`
  (`Day`), `stater` (states `empty`, `entering`, `working`, `waiting-to-leave`, `leaving`, `done`), `boothClock`
  (the day's clock) and `criminalPoster`.
- `Paper`: `def` (`data.PaperDef`), `deskItem`, `pageIndex`, `idWithIndex`. `PaperDef`: `id` (`Rules`, `Bulletin`,
  `Passport`, ...), `stampable`, and `pages` (every page the paper has; the day's `PaperNav.hasPage` says which
  show). `pageIndex` indexes `def.pages`.
  - `Paper.setPage(i)` (0xD93F30) clamps `i` to `def.pages`, then walks forward to the first page the day's nav
    has, so an index the day lacks lands on the next page it has. `get_pageId` (0xD94280) is
    `def.pages[pageIndex].id`, and `PaperDef.findPageIndex(id)` (0xD99510) searches every page, shown or not.
    `Paper.updatePage()` (0xD959A0) rebuilds the page's visuals; a page is otherwise drawn only when turned to.
  - Page ids: the rulebook's contents page is `home`; the bulletin's are `notice0`, `notice1`, ... and
    `criminals` (WANTED CRIMINALS; page 4 of 5 on day 24).
- `DeskItem`: `placement` (`data.DeskItemPlacement`: OUTER 0 = on the counter (small), INNER 1 = on the desk
  (open), FILER 2, MOUNT 3), `state` (`data.DeskItemState`: NONE 0, REVEALING 1, NORMAL 2, RETURNING_TO_DOCK 3,
  HIDING 4, HIDDEN 5, GIVING 6, GIVEN 7), `dragging`, `visible`. Methods:
  - `set_placement(p)` (0xCE4FE0); `updateClipRects()` (0xCE8580) after a move.
  - `updateCustomDrag(dt)` (0xCE9D00) moves the item every frame while it is held.
  - `returnToDock()` (0xCE4F40) sends the bulletin back to its dock on the counter when a traveler is called. It is
    called through a closure; it was found by hooking `set_placement` and logging when the locked bulletin moved.
- An item's position is `app.ent.Ent.pos`, an `app.vis.PointData` holding x, y as doubles in booth coordinates.
  On day 24 the bulletin starts INNER at 298,2 open at `notice0`, and the rulebook OUTER at 113,174 at `home`.
  Each day reveals them at fixed places, the bulletin opened at the page an `OpenToPage` fact names. The lock does
  not hook any of that: it places an item when a booth's paper is first seen settled, which covers a fresh day
  and a loaded one alike (both checked: day 24 loaded, day 25 fresh).

### Judgement and citations
- A document handed back goes GIVING, then GIVEN, then `Booth.deskItem_onGiven(desk)` (0xBB7D00) passes it to
  `BoothEngine.givePaperToTraveler(idWithIndex)` (0xCF8D60). That adds the paper's stamp approval to
  `givenApproval` only for a `stampable` def; nothing else ever adds to it. So a paper that is never handed back,
  such as a passport in the filer or one left on the desk, never counts in the verdict.
- `PaperDef` flags: `tradeForPaperId` +0x30, `fromTraveler` +0x38, `stampable` +0x48. Only the passport is
  stampable among a usual traveler's papers. `BoothEngine.canGivePaperToTraveler` (0xCF8B30) refuses everything
  until the inspector has stamped something (`haveStampedSomething`), and a stampable paper until it has an
  approve or deny stamp. Trade papers, and papers the traveler asks for (`Traveler.canGivePaper`), are exceptions.
- Confiscation: dropping a paper in the filer calls `BoothEngine.putPaperInFiler(id, firstTime)` (0xCF9320), which
  pushes the id into `confiscatedPaperIds` at once (`removePaperFromFiler` takes it out again). The first time a
  passport is filed, it adds a **SeizureSlip** paper: the inspector's own (not `fromTraveler`), stamped with the
  verdict and handed over in the passport's place ("PASSPORT SEIZURE slips will be issued to allow entry or denial
  without a passport", `StreamingAssets/loc/en.zip`, `/data/Loc.csv`).
- `mistakeCaught` is set by `BoothEngine.stampPaper` (0xCF89C0) when a stamp contradicts `hasErrors` at that moment
  (approved with errors, or denied without). It is cleared only with the traveler, so it is settled by any
  hand-back.
- Once everything is back and the passport has its verdict, the booth goes `working` → `waiting-to-leave`
  (`Booth.engine_onWantLeaveEarly` sends it there directly, papers or not). `Booth.hideTransientPapers()` (0xBB8CB0)
  clears the desk, and a passport still on it goes to the filer when `BoothEngine.shouldAutoConfiscatePaper(def)`
  (0xCF72A0) says so: on FILER days, for a passport-type paper, once something stamped has been given. The traveler
  then leaves, and only then does `BoothEngine.handleEvent(PROCESSING_FINISH)` (0xCF4970) judge. That queues the
  citation (the printer prints queued engine ops as they apply, at once when applied with `fromQue`) and sets
  `justMadeMistake`. In an endless mode it calls `Endless.handleAction` (0xF23E90) with `"mistake"` (any citation),
  `"detain"` or `"process"`, and a Perfection run ends on `"mistake"`. That call is the only `handleAction` caller
  besides the by-name dispatcher `__hx_invokeField`.
- The judgement, as `failhold.cpp`'s `mistake_pending` mirrors it. Nothing applies when `traveler.forceDeny` is
  set, except the last rule:
  - on days with the FILER feature: `hadPaper("Passport")` and `wantConfiscatePassport()` disagreeing with
    whether the passport is confiscated (`confiscatedPaperIds` has `Passport`);
  - `mistakeCaught && !traveler.detained` and approved == `env.hasErrors(null)` (a wrong stamp);
  - on days with the DENIAL_REASON feature: denied without a reason.

  `StampApproval.flags` is a bit per `StampApprovalKind`: 2 approved, 4 denied, 8 reasoned. `combine(a, b)`
  (0xECA9D0) is static.
- `Day.featureFlags`: bit n is `play.day.Feature` index n. FILER is 5, CRIMINALS 8, DENIAL_REASON 9.
- `app.Clock`: the time is `sysCurTime - sysStartTime` (its `time` field is unused). `update(forceDeltaTime)`
  (0x1042340) advances it, and `pauseForOneFrame()` (0x1042660) takes the last step back. `Endless.clock` is an
  endless run's timer.

### Wanted criminals
- `Day.criminals`: an Array of `play.day.Criminal` {`index`, `used`}. `used` is set once the criminal has been
  sent to the booth as a traveler today. `Traveler.criminalIndex` is the wanted criminal a traveler is (-1 for
  none).
- The bulletin's photos come from `BoothEnv.getImage(paperId, factId, scale, textColor, backColor)` (0xD0F210) for
  facts `Criminal<slot>`. `BoothEnv.getFactValueText("Bulletin/Criminal<slot>", "")` (0xD07460) holds the index into
  the day's criminals; an empty value is a blank slot. If getImage returns null, the page shows its own empty photo
  box. (The game's blank-photo image is bigger than the box and spills into the next slot, so the mod returns null
  rather than that image.)
- `CriminalPoster.sprites`: one `app.vis.Visual` per criminal, in order. The mod hides them with the photos.

## The features

### Lock Inspector Materials (`lock.cpp`)
- Saved per item (`LockRules`, `LockBulletin` in the ini): `x,y,placement,page,page_id`.
- **Capture.** Switching the lock on in a booth marks both items for capture. Each is captured once it is settled:
  state NORMAL and not dragging. A capture still pending when the booth goes (an item that never settled there) is
  dropped, and that item keeps its last lock. With the switch on and nothing saved for an item, it is captured
  where it first settles. A capture writes the ini.
- **No re-lock button.** The panel had one, Lock where they are now, until 2026-10-26, when the user had it removed
  ("remove that useless option"): with the lock on the items cannot move, so it could only re-save the pages, and
  switching the lock off and on again does the same. To move the items or change their pages: lock off, arrange,
  lock on.
- **Holding.** `updateCustomDrag` and `returnToDock` do nothing for a locked item. The first time a booth's paper is
  seen settled, it is placed: placement (`set_placement`), position, `updateClipRects`, and then the page, found by
  id through `findPageIndex` (the saved index if the id is missing; `setPage` does the rest). After that the page
  is the player's. If anything moves the item anyway, it is put back. So a rulebook locked on the counter (OUTER,
  where day 24 starts it) cannot be opened: the drag is skipped, and a change of placement is undone. The README
  says to open it on the desk before locking.
- The panel's note names the pages the way they print ("the contents", "notice 2", "the wanted criminals").

### Cross-Off Criminals (`criminals.cpp`)
- Each tick computes the found mask: on days with the CRIMINALS feature, the criminals that are `used` and are not
  the traveler at the window. The `getImage` hook returns null for a `Bulletin`/`Criminal<slot>` fact whose
  criminal is in the mask.
- When the mask changes (or the switch does), the bulletin gets `updatePage()` and the poster's sprites are shown
  or hidden. A new booth draws its bulletin with the hook already in place.

### Pause game after failure (`failhold.cpp`)
- Hooks: `Booth.deskItem_onGiven`, `BoothEngine.handleEvent`, `Endless.handleAction` and `Clock.update`.
- **When to judge.** At a hand-back of one of the booth's papers (switch on, not the traveler a hold already
  continued for), and only once the verdict is settled. `verdict_open` checks that no stampable paper except the
  one handed back is still on the desk; papers in the filer, or GIVING, GIVEN, HIDING or HIDDEN, don't count.
  Until then the outcome can still change: the passport may yet be filed or stamped again, or a confiscated
  passport's seizure slip still needs its stamp. So the hold waits for the next hand-back. A passport handed back
  first is judged at once, with the other papers still on the desk; one handed back last brings the hold with it.
  Until 2026-10-25 the mod judged at the first hand-back. On a confiscation entrant that could hold for a citation
  the game would never have given, since it counted, and gave early, the stamps of a passport bound for the filer.
- **The prediction** (`mistake_pending`) applies the judgement's rules to what the traveler leaves with: the
  approvals given so far, plus those of the stamped papers on their way (the one handed back, already GIVEN when
  the booth hears of it, and any still GIVING).
- **The hold.** The document is kept: its state goes back to NORMAL and it is made visible, so it falls back onto
  the counter, and the engine never hears of it. Then the judgement runs **now**. The approvals on their way go
  into `givenApproval`, as `givePaperToTraveler` would add them, and `handleEvent(PROCESSING_FINISH)` runs with any
  endless `handleAction` it makes deferred. The citation prints at once. If `justMadeMistake` stays false, the
  prediction was wrong and the hold ends at once (logged). During the hold every hand-back is kept, and after each
  update the booth clock and an endless run's clock get `pauseForOneFrame`.
- **Continue** (or the switch going off): the kept documents go to GIVING (the game's own giving finishes them)
  and the deferred endless action is replayed. The traveler's later `PROCESSING_FINISH` is skipped, since that
  judgement already ran, so there is exactly one citation. Leaving the booth during a hold drops it. If the
  traveler goes during a hold (detained), the hold is dropped and the deferred endless action is replayed.
- **Markers.** `g_continued` (the traveler a hold was continued for) and `g_judged` (the traveler whose departure
  judgement is skipped) are raw pointers. The tick lets them go as soon as the booth or its traveler changes,
  because a traveler that has gone is freed and a new one can be given its address. Until 2026-10-25 they lived on
  across booths. The user's report from that day fits this: a Perfection run whose first mistake went unheld, the
  run right after a hold the user left for the main menu.
- **Misses are logged** as "failhold: the game cited a mistake at the departure that was not held - ..." with the
  reason:
  - nothing was handed back (detained, left early);
  - a stamped paper was still on the desk at the last hand-back (an auto-confiscated passport);
  - no mistake was foreseen (a gap in the prediction: investigate);
  - the switch was off.
- Verified in game (day 24, and the endless Perfection mode): holds whose citations printed while the papers
  stayed, "Protocol Violated. Passport not confiscated / Applicant clear for entry" (a clean applicant denied, and
  their passport handed back where the day's rule wanted it confiscated) and "Applicant clear for entry" (the
  wrong-stamp text, `env.getCiteText()`); one citation each after Continue; the booth clock and the endless timer
  frozen; the Perfection run ending at Continue. The denial-reason rule (its text is the language table's, not
  getCiteText) is mirrored from the code but has not been seen to fire yet. The settled-verdict timing, the
  marker clean-up and the miss log (2026-10-25) were built from the game's code; the user tests them in play.
- Held only at a hand-back: a citation the game makes another way is left to it (and logged).

## The panel (`panel.cpp`)

The panel is drawn while `Game.pauseScreen` is up (or with `AlwaysShow`), unless the toggle key hid it.
`###PPCabbyCodes` is placed at 1 em from the top-left on first use, then wherever the player leaves it
(`PPCabbyCodes.imgui.ini`). It is 24 em wide and grows downward. Its title bar's arrow, or a double-click on the
title, folds it down to the title bar (the user, 2026-10-25: "update the window so it can be minimized"). ImGui
keeps the fold with the place in the ini (`Collapsed=1`, written only while folded). The overlay never destroys its
context, so the ini is written only by ImGui's timer, set to 1 s (`IniSavingRate`; ImGui's default is 5 s) so that a
fold made just before quitting is kept. Each switch has a wrapped tooltip (26 em), greyed-out switches included.
A greyed-out switch is one whose start failed; the log says why. During a hold the Continue box is at the top
right, clear of the papers, the printer and an endless run's scoreboard (top centre), and it hides while the pause
menu is up. It has no title bar and does not fold.

## Every hook and call into the game

- **Hooks**: `HostUnity.Update`, `InputWrapper.GetPointerState` (0xD45260), `DeskItem.updateCustomDrag`,
  `DeskItem.returnToDock`, `BoothEnv.getImage`, `Booth.deskItem_onGiven`, `BoothEngine.handleEvent`,
  `Endless.handleAction`, `Clock.update`, and the present: `glXSwapBuffers` on Linux; on Windows the DXGI factory
  class's QueryInterface, CreateSwapChain and CreateSwapChainForHwnd vtable slots (in place) and each game swap
  chain's own vtable copy. Under the harness only: `InputWrapper.GetKeyState` (0xD45200),
  `SteamAPI.RestartAppIfNecessary` and `SteamAPI.Init`. How it gets in: on Windows UnityPlayer.dll's import slot for
  `GetProcAddress`, in front of a mod loader's and calling on to it; on Linux, fallbacks only: `il2cpp_init`, and
  UnityPlayer's `dlsym`.
- **Calls**: `Paper.setPage`/`get_pageId`/`updatePage`, `PaperDef.findPageIndex`, `DeskItem.set_placement`/
  `updateClipRects`, `BoothEnv.getFactValueText`/`hasErrors`/`wantConfiscatePassport`/`hadPaper`,
  `BoothEngine.get_givenApproval`/`set_givenApproval`/`getPaperApproval`, `StampApproval.combine`,
  `Clock.pauseForOneFrame`, and the game's own
  `handleEvent(PROCESSING_FINISH)` and `Endless.handleAction` (the hold's early judgement and its replay).
- **Field writes**: `Ent.pos` (lock), `DeskItem.state`/`visible` (the hold), `Visual.visible` (the poster). Under
  the harness only: `Clock.sysStartTime`, `Criminal.used`, and the engine's traveler count.

## The Windows build (depot 239031)

- The same Unity (2020.3.34f1), IL2CPP and game version; `GameAssembly.dll` exports the same `il2cpp_*` API. Its
  `global-metadata.dat` differs from Linux's only in the string literals' count (byte 14 of the header): every class,
  field and method the mod uses is found by name (`pp: ... all there`).
- UnityPlayer.dll loads dxgi.dll and d3d11.dll itself: `CreateDXGIFactory2` (else 1, else the plain one) and
  `D3D11CreateDevice`, through GetProcAddress. Direct3D 11.0 at feature level 11.1. Its imports: ADVAPI32, bcrypt,
  CRYPT32, dwmapi, GDI32, HID, IMM32, KERNEL32, ole32, OLEAUT32, OPENGL32, SETUPAPI, SHELL32, SHLWAPI, USER32,
  VERSION, WINHTTP, WINMM, WS2_32. GameAssembly.dll imports baselib.dll (the game's), IPHLPAPI and system DLLs.
- The saves: `%APPDATA%\3909\PapersPlease` (the Linux build's are in the same format: they load). Steam's DLL is
  `PapersPlease_Data/Plugins/x86_64/steam_api64.dll`. Under plain Wine with no Steam, `SteamAPI_Init` fails and the
  game goes on.

## Tooling notes

- **mingw-w64** (GCC 16, posix threads, SEH exceptions, UCRT) builds the Windows DLL and tests; the system's **Wine**
  (11, staging) runs them. DXVK comes from Proton Experimental's files (above).

- **Il2CppDumper** v6.7.46 (net7 build, run with `dotnet Il2CppDumper.dll GameAssembly.so
  PapersPlease_Data/il2cpp_data/Metadata/global-metadata.dat <out>`) writes `dump.cs` (classes, field offsets,
  method RVAs), `il2cpp.h`, `script.json`, `stringliteral.json`. They were kept in the session scratchpad; `dump/`
  is gitignored should they move into the project.
- **Ghidra** (`/opt/ghidra`), project `ghidra_proj/` (gitignored). The import is
  `analyzeHeadless ghidra_proj PP -import GameAssembly.so -loader ElfLoader -loader-imagebase 0 -scriptPath
  tools/ghidra -preScript Il2CppApply.java <script.json> <il2cpp_ghidra.h>`, with the header made parseable first
  by `tools/ghidra/fix_header.py`. The image base is 0, so Ghidra's addresses are dump.cs's RVAs. Full
  auto-analysis of the 18 MB library takes a long time (tens of minutes). After it, `FixNoReturn.java 0x780b00` is
  needed: Ghidra takes IL2CPP's GC write barrier (`il2cpp_gc_wbarrier_set_field`, a card mark and `ret`) for a
  function that never returns and cuts every method off at its first field store.
- **Queries**: `tools/ghidra/run.sh dec|xref|find|grep ...` (`Report.java`). Names are as Il2CppDumper gives them
  (`play.day.booth.Paper$$setPage`). A run takes about 5 s, and its answer is left in `ghidra_proj/report.txt`.
