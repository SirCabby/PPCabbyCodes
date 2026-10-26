#pragma once

#include <atomic>

// The panel's switches. The panel (render thread) flips them; the game's
// thread reads them every frame.
namespace ppcc::switches {

enum Kind { kLockMaterials, kCrossOffCriminals, kPauseAfterFailure, kCount };

inline std::atomic<bool> on[kCount];

inline bool get(Kind k) { return on[k].load(std::memory_order_relaxed); }

}  // namespace ppcc::switches
