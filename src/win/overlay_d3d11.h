#pragma once

// The Windows renderer's way in (win/overlay_d3d11.cpp): UnityPlayer.dll looks
// up dxgi.dll's factory functions through GetProcAddress, where the mod sits
// (win/main.cpp).
namespace ppcc::overlay {

// For an export of dxgi.dll that UnityPlayer asked for: the overlay's own
// CreateDXGIFactory, CreateDXGIFactory1 or CreateDXGIFactory2 (each makes the
// factory, then takes its swap chains as they are made), or `real` for any
// other name.
void* dxgi_export(const char* name, void* real);

}  // namespace ppcc::overlay
