#pragma once

// hid.dll: what the mod stands in for on Windows. UnityPlayer.dll imports
// HID.DLL (for game controllers), and Windows looks for it beside the exe before
// System32 - it is not one of the KnownDLLs - so the mod, named hid.dll beside
// PapersPlease.exe, is loaded with the game, before any of its code runs, and
// without replacing a file of the game's. No mod loader takes that name:
// MelonLoader is version.dll there (or another of its thirteen names), BepInEx's
// Doorstop winhttp.dll. Every export of Windows' own hid.dll (hid.def) jumps on
// to that DLL, in whatever process loads the mod, so every module that uses
// hid.dll - the game's input, DirectInput, Steam's overlay - gets Windows' own.
namespace ppcc::proxy {

// Loads System32's hid.dll and points each export at its function. Once, from
// DllMain.
void load();

// Whether the real DLL was found (false: every export fails).
bool loaded();

}  // namespace ppcc::proxy
