[size=6][b]PPCabbyCodes[/b][/size]

An [b]option panel[/b] for [b]Papers, Please[/b] on Steam. Pause the game and a small panel appears over the pause menu.

This mod is open source! Check it out at [url=https://github.com/SirCabby/PPCabbyCodes]https://github.com/SirCabby/PPCabbyCodes[/url]

[size=5][b]What it does[/b][/size]
[list]
[*][b]Lock Inspector Materials[/b] - Remember your rulebook and newspaper positions on your desk at the start of a new day.  Also makes it so they cannot be moved while the lock is on.[/*]
[*][b]Cross-Off Criminals[/b] - Any wanted criminals that have already been judged are removed from the newspaper.[/*]
[*][b]Pause game after failure[/b] - Lets you inspect what was wrong after getting a citation before taking the papers away.  Pauses the game timer during this phase.  Press the Continue button to release it.[/*]
[*][b]F7[/b] hides or shows the option panel while in the pause menu.[/*]
[/list]

[size=5][b]Install[/b][/size]

Open the game's folder (in Steam: right-click the game > Manage > Browse local files), then:
[list]
[*][b]Windows[/b]: copy [b]hid.dll[/b] (from the download's Windows folder) into that folder, beside PapersPlease.exe.[/*]
[*][b]Linux[/b] (the native version): copy [b]libPPCabbyCodes.so[/b] (from the download's Linux folder) into its [b]PapersPlease_Data/Plugins[/b] folder.[/*]
[/list]

To uninstall: delete hid.dll (Windows) or PapersPlease_Data/Plugins/libPPCabbyCodes.so (Linux). Its settings and log in the game folder (PPCabbyCodes.*) can go too.

[size=5][b]If it does not work[/b][/size]

The mod says what it found, and why a switch is greyed out, in [b]PPCabbyCodes.log[/b] in the game's folder (the start before is kept as [b]PPCabbyCodes.prev.log[/b]). Send both with a report.

[size=5][b]Config[/b][/size]

[code]
LockInspectorMaterials = 0  ; each switch's state when the game starts - the panel writes changes back here
CrossOffCriminals      = 0
PauseAfterFailure      = 0
ToggleKey              = F7  ; hides/shows the panel: F1-F15, Insert, Home, End, PageUp, PageDown, Pause,
                            ; ScrollLock, Backquote, or a Unity KeyCode number
AlwaysShow            = 0  ; debug: the panel on every screen, not only while paused
Disable                =    ; comma list of the mod's parts to turn off: overlay,input,features[/code]

Lives in [b]PPCabbyCodes.ini[/b] in the game folder (beside PapersPlease.exe on Windows; created on the first start). The panel adds LockRules and LockBulletin: where the locked rulebook and bulletin go and the pages they open at. Beside it the mod keeps PPCabbyCodes.log (what it found and did) and PPCabbyCodes.imgui.ini (where the panel is, and whether it is folded).
