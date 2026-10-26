#pragma once

#include <string>

// A test harness for driving the game without a person at it (tools/headless.sh
// runs the game in a nested, headless KWin and talks to it through files).
// Inert unless the environment has PPCC_TEST_DIR: then every frame the mod
// reads $PPCC_TEST_DIR/cmd (if there), runs its lines over the next frames and
// answers in $PPCC_TEST_DIR/ack ($PPCC_TEST_DIR/abort drops what is queued).
// Commands, one per line:
//
//   move X Y             the pointer to window pixel X,Y (top-left origin)
//   press / release      the left button
//   click X Y            move, press, release (a few frames each)
//   drag X1 Y1 X2 Y2 [N] press at 1, move to 2 in N steps, release
//   key NAME             a key press the game sees (ESCAPE, ENTER, SPACE, TAB, LEFT, RIGHT, T)
//   wait N               N frames
//   ms N                 N milliseconds (frames are no clock: a headless game may run uncapped)
//   screen NAME [MS]     until the game's screen is that class (TitleScreen, LoadScreen, NewsScreen,
//                        DayScreen), at most MS milliseconds (60000)
//   booth STATE [MS]     until the booth's state machine is in STATE (empty, entering, working,
//                        waiting-to-leave, leaving, done), at most MS milliseconds (60000)
//   continue             click the hold's Continue button where the overlay drew it
//   shot NAME            the next frame as $PPCC_TEST_DIR/NAME.ppm
//   state                the game's state into the log
//   switch NAME 0|1      a panel switch (lock, crossoff, pause)
//   request NAME         a panel request (continue)
//   panel 0|1            show the panel without pausing
//   crimused N           today's wanted criminal N counts as having been at the booth
//   clock S              the booth's clock jumps S seconds ahead
//   travelers N          the day counts N travelers processed (a day ends only after its minimum)
//   throw                a managed exception raised by the game's own code inside the mod's frame
//                        (il2cpp_raise_exception): the frame's guard must catch it, and the mod's
//                        frame stops for good - so this is the run's last command (its batch is
//                        answered first)
namespace ppcc::testkit {

bool active();
void init();  // reads the environment (from game::start)

// Runs the queue one frame on (the game's thread, in the mod's frame).
void tick();

// The pointer and keys the harness holds, for the game's input hooks.
struct Pointer {
  bool active = false;  // the harness has moved the pointer at least once
  float x = 0, y = 0;   // window pixels, top-left origin
  bool down = false;
};
Pointer pointer();
bool key_held(int game_key);

// A screenshot the render thread should take of the next frame: the file path,
// or empty. Taken (cleared) by the overlay.
std::string take_shot_request();

bool force_panel();

}  // namespace ppcc::testkit
