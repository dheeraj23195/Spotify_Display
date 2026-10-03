#pragma once
#include <time.h>

// Starts NTP with the Asia/Kolkata time zone (UTC+5:30, no DST).
// Call once Wi-Fi is connected; syncing continues in the background.
void timeBegin();

// True once the clock has been set from the network.
bool timeIsSet();

// Local time (Asia/Kolkata). Returns false if the clock is not set yet.
bool timeNow(struct tm &out);
