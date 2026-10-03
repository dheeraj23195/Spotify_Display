#pragma once

// Last-resort self-recovery: restart the display if Spotify has not answered for
// several minutes. Never restarts while a firmware update is running, and gives
// up after a few restarts in a row so a lasting outage cannot cause a boot loop.

void healthBegin();             // call early in setup()
void healthPollOk();            // network task: Spotify gave a real answer
uint32_t healthSinceOkMs();     // time since the last real answer
void healthCheck(bool busy);    // render loop, every frame; busy = update running
