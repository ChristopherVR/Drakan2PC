#pragma once
class PS2Runtime;
namespace drakan_dragon_flight {
// Arokh's flight look-ahead (PAL, drakan_dragon_flight_policy.h): a look-ahead hit on an object whose box holds the
// start of the ray no longer stops him (an arrival inside a converted level's object left him unable to move).
// DRAKAN_DRAGON_RAY_TRACE=1 also logs every look-ahead ray (bounded, DRAKAN_DRAGON_RAY_LINES, default 400).
void install(PS2Runtime& runtime);
} // namespace drakan_dragon_flight
