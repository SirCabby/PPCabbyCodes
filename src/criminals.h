#pragma once

#include <string>

#include "il2cpp.h"

// Cross-Off Criminals: the bulletin's wanted criminals who have already been
// found lose their photos. The bulletin draws each photo through
// BoothEnv.getImage("Bulletin", "Criminal<n>"), whose fact names a criminal of
// the day; for a found criminal the mod answers with no image, which leaves
// the page's own empty photo box. Found: the criminal has been at the booth (the game marks
// Criminal.used when one becomes the traveler) and is not the traveler being
// inspected now - so a photo never vanishes while its owner is at the window.
// The booth's wanted poster (pinned from the bulletin) hides theirs too.
namespace ppcc::criminals {

bool start();  // hooks BoothEnv.getImage
void tick(il2cpp::Object* booth, bool on);
std::string describe();

}  // namespace ppcc::criminals
