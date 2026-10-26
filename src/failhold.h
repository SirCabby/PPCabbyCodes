#pragma once

#include <string>

#include "il2cpp.h"

// Pause game after failure. Once the verdict is settled - the last stamped
// paper on the desk (the passport, or the seizure slip of a confiscated one) is
// handed back - the judgement the game makes at the traveler's departure can be
// foreseen. If it will cite the inspector (a wrong stamp, a passport wrongly
// confiscated or not, a denial without the reason the day requires), the booth
// holds at that hand-back: the traveler does not take the document (it drops
// back on the counter, beside whatever is still on the desk), the day's clock -
// and an endless run's - stops, and the citation prints at once. Continue lets
// the traveler take it and carries on as the game meant: everything handed back
// from then on is taken, the traveler leaves, and an endless run's penalty or
// ending for the mistake applies then.
//
// The citation is the game's own: the mod runs the engine's end-of-traveler
// judgement (BoothEngine.handleEvent(PROCESSING_FINISH)) at the hold - its
// citation, stats and counts - with the endless run's handleAction kept back
// until Continue, and skips the judgement when the game calls it after the
// traveler has walked off. A citation the hold did not catch is logged with the
// reason.
namespace ppcc::failhold {

bool start();
void tick(il2cpp::Object* game, il2cpp::Object* booth, bool on, bool continue_requested);
bool holding();
std::string describe();

}  // namespace ppcc::failhold
