# PPCabbyCodes

A cheat panel for **Papers, Please** on Steam (app `239030`), for Windows and the native Linux version. Pause the
game and a small panel appears over the pause menu.

## What it does

- **Lock Inspector Materials** - the rulebook and the bulletin stay where you put them. Switch it on in the booth
  and it locks them where they are, with the pages they are open at (switched on anywhere else, the next day puts
  them back where they were last locked, or, the first time, locks them where they lie). Dragging them then moves
  nothing, and the game no longer sends the bulletin back to the counter when you call the next entrant. You can
  still turn their pages: the lock remembers a page without holding you to it. Every day, a new one or one you
  load, starts with both in their locked places, open at the locked pages. If the day's paper has no such page, it
  opens at the next page it does have. To move them, or to change the pages they open at, switch the lock off,
  arrange them and switch it on again. Open the rulebook on the desk before you lock it: one locked on the counter
  can't be opened.
- **Cross-Off Criminals** - on the bulletin's WANTED CRIMINALS page, each wanted criminal who has already been
  found loses their photo, leaving the empty photo box. Found means they have come through your booth today. The one
  standing at your window keeps their photo until they leave, so you can still compare them. Switch it off and the
  photos come back.
- **Pause game after failure** - when the game is going to cite you for an entrant, the mod stops as soon as your
  verdict is settled, instead of letting the entrant leave. That happens when you hand back the stamped passport,
  or, for a confiscated passport, the stamped seizure slip. That document drops onto the counter, whatever is still
  on the desk stays there, the clock stops (and an endless run's timer) and the citation prints at once, so you can
  see what it was for and look over the papers again. Hand the passport back first and the rest of the papers are
  still there to look at. Papers you hand back during the hold stay too. A small
  **Continue** button at the top right of the screen carries on as the game meant to: the papers you handed back
  go to the entrant (hand over the rest as usual), and the game does whatever it does after a mistake (an endless
  mode may end the run there). The citation is the game's own and counts as usual, and it is printed only once.
  It catches the mistakes the game cites at an entrant's departure: the wrong stamp, a passport that should or
  should not have been confiscated, a denial without a reason on days that ask for one. A citation that comes
  without a hand-back (an entrant you detain, say) isn't held, and the log notes it.

**F7** hides or shows the panel while the game is paused. The panel grows with the game's window and can be dragged
anywhere. The arrow at the left of its title bar (or a double-click on the title) folds it down to its title bar,
and opens it again. Its switches, place and fold are remembered between starts. Hover over a switch for a reminder
of what it does.

## Install

Open the game's folder (in Steam: right-click the game > Manage > Browse local files), then:

- **Windows**: copy **hid.dll** (from the download's `Windows` folder) into that folder, beside
  `PapersPlease.exe`.
- **Linux** (the native version): copy **libPPCabbyCodes.so** (from the download's `Linux` folder) into its
  **PapersPlease_Data/Plugins** folder.

That is all: the game loads it by itself, nothing of the game's is replaced, and no launch options are needed.

On Windows the mod is named `hid.dll` because that is a file the game looks for in its own folder before Windows'
own (Windows' `hid.dll`, which handles game controllers, is still the one doing that work: the mod passes
everything on to it).

It works beside **MelonLoader** and **BepInEx** and their mods (the Discrepancy And Story Assistant, the Custom
Traveler Engine, Always Have All Upgrades...): it is none of their files (MelonLoader is `version.dll`, BepInEx
`winhttp.dll`), and where both hook the game's start the mod lets the loader do its part first. Both were tried
with it: MelonLoader 0.7.3 with a mod of its own patching two of the methods this mod hooks, and BepInEx 6.

The Windows version run under Proton needs one launch option, which Proton asks for any file of this kind:
`WINEDLLOVERRIDES="hid=n,b" %command%` (with MelonLoader `"hid,version=n,b"`, with BepInEx `"hid,winhttp=n,b"`).
On Linux the native version with `libPPCabbyCodes.so` needs none.

To uninstall: delete `hid.dll` (Windows) or `PapersPlease_Data/Plugins/libPPCabbyCodes.so` (Linux). Its settings
and log in the game folder (`PPCabbyCodes.*`) can go too.

It is made for the current Steam version of the game (1.4.11.124, build 10817815). It finds everything it uses by
name, so it may keep working after an update; anything it cannot find, it leaves alone.

### If it does not work

The mod says what it found, and why a switch is greyed out, in **PPCabbyCodes.log** in the game's folder (the
start before is kept as **PPCabbyCodes.prev.log**). Send both with a report.

## Please read before using

- This first version was checked in the running game by the mod's own test harness, on the story's days and in
  endless mode, but it has not had a real play-through yet. Expect rough edges, and report them with the log.
- The Windows version was checked the same way, in the Windows game run under Wine (with Wine's Direct3D and with
  DXVK's, and beside MelonLoader and BepInEx), but not yet on Windows itself.
- The panel is drawn with what the game draws with by default: Direct3D 11 on Windows, OpenGL on Linux. Made to
  run on anything else (Vulkan, or Direct3D 12 on Windows), the game shows no panel, but the switches still do what
  `PPCabbyCodes.ini` says.
- The mod never writes your saves; the game saves as it always does. A citation during a hold is the game's own,
  and it counts.

## Config

`PPCabbyCodes.ini` in the game folder (beside `PapersPlease.exe` on Windows), written with every option
documented on the first start:

```ini
LockInspectorMaterials = 0   ; each switch's state when the game starts - the panel writes changes back here
CrossOffCriminals      = 0
PauseAfterFailure      = 0
ToggleKey              = F7  ; hides/shows the panel: F1-F15, Insert, Home, End, PageUp, PageDown, Pause,
                             ; ScrollLock, Backquote, or a Unity KeyCode number
AlwaysShow             = 0   ; debug: the panel on every screen, not only while paused
Disable                =     ; comma list of the mod's parts to turn off: overlay,input,features
```

The panel adds `LockRules` and `LockBulletin`: where the locked rulebook and bulletin go and the pages they open
at. Beside the ini the mod keeps `PPCabbyCodes.log` (what it found and did) and `PPCabbyCodes.imgui.ini` (where the
panel is, and whether it is folded).

## Building from source

Built on Linux with g++ (C++20); the Windows build is cross-compiled with mingw-w64:

```sh
cp config.mk.example config.mk   # set GAME_DIR to your game folder (and WIN_GAME_DIR to a Windows copy)
make                             # build/libPPCabbyCodes.so and build/win/hid.dll
make test                        # the offline tests (the Windows ones under Wine, when it is installed)
make install                     # the Linux build into PapersPlease_Data/Plugins, the Windows one into WIN_GAME_DIR
make uninstall                   # removes them again
make dist                        # dist/PPCabbyCodes/: Linux/, Windows/, the docs
```

`tools/headless.sh` runs the game in a hidden nested desktop and drives it through the mod's test harness
(`WIN=1` runs the Windows version under Wine); `tools/ghidra/` has the scripts used to read the game's code.
CLAUDE.md has the details.

## License

GPL-3.0 - see [LICENSE](LICENSE). Dear ImGui (`contrib/imgui`) is MIT-licensed; the Hacker Disassembler Engine
(`contrib/hde`, from MinHook) is BSD-licensed.
