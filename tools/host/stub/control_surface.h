#pragma once
#include <cstdint>
struct FootswitchEvent { bool down=false, rising=false, falling=false; uint32_t held_ms=0; };
class ControlSurface {
 public:
  float knob[6] = {0.5f,0.5f,0.5f,0.5f,0.5f,0.5f};
  int   sw[3]   = {0,0,2};
  FootswitchEvent fs[2];
  float Knob(int i) const { return knob[i]; }
  int   Switch(int i) const { return sw[i]; }
  const FootswitchEvent& Foot(int i) const { return fs[i]; }
  bool  BothHeld(uint32_t) const { return false; }
};
