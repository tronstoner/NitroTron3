#pragma once
//
// core/util/tunable.h
//
// Live-tunable compile-time constants (docs/DEV_INTERFACE.md, step 2).
//
//   TUNABLE(type, NAME, value);      // type: float, int or bool
//
//   * Normal builds (and every host build: tools/host, tools/hostsim):
//       constexpr type NAME = value;
//     exactly - the binaries are byte-identical to a plain constexpr.
//   * Firmware DIAG builds (the Makefile adds -DLIVE_TUNABLES with DIAG=1):
//       a RAM variable initialised to the default and registered in a linked
//       list (name, type, pointer, default) that the diag link walks
//       (src/core/io/diag_link.h: list / get / set / reset). A set is one
//       32-bit (or 8-bit, bool) store from the main loop; the audio code reads
//       the variable on its next use. tunable::Gen() counts sets / resets so a
//       module can re-derive values it caches from tunables (explorer.h
//       TunableRefresh).
//
// Code that uses a tunable in a constant expression cannot compile in a DIAG
// build. For a `constexpr` local / function that reads a tunable, use
//   TUNABLE_CONSTEXPR     -> `constexpr` normally, `const` in DIAG   (variables)
//   TUNABLE_CONSTEXPR_FN  -> `constexpr` normally, `inline` in DIAG  (functions)
// (identical code in normal builds). Array sizes and static_assert inputs stay
// plain constexpr.
//
// The registry metadata (unit, range, taper, level) is a comment tag on the
// declaration line, read by tools/devtool (never compiled in):
//   TUNABLE(float, X, 0.3f);   // @unit s @range 0.1 1 @taper log @level live  <note>
//
// C++14: no inline variables. The constants files are included by exactly one
// translation unit per pedal (the shell's main.cpp), so `static` variables and
// nodes at namespace scope are fine; registration order = declaration order.

#ifdef LIVE_TUNABLES

#include <stdint.h>
#include <string.h>

namespace tunable {

struct Node;
inline Node*& Head() { static Node* h = nullptr; return h; }   // constant-initialised (no guard)
inline Node*& Tail() { static Node* t = nullptr; return t; }
inline volatile uint32_t& Gen() { static volatile uint32_t g = 0; return g; }   // ++ on every set / reset
inline int& Count() { static int n = 0; return n; }

struct Node {
  const char* name;
  void*       ptr;
  uint32_t    def;      // default, raw bits (float / int / bool in the low byte)
  char        ty;       // 'f' float, 'i' int, 'b' bool
  bool        echo;     // diag link: a `T` line for this one is due
  Node*       next;
  Node(const char* n, float* p) : name(n), ptr(p), def(0), ty('f'), echo(false), next(nullptr) {
    memcpy(&def, p, 4); Link();
  }
  Node(const char* n, int* p) : name(n), ptr(p), def(0), ty('i'), echo(false), next(nullptr) {
    memcpy(&def, p, 4); Link();
  }
  Node(const char* n, bool* p) : name(n), ptr(p), def(*p ? 1u : 0u), ty('b'), echo(false), next(nullptr) {
    Link();
  }
  void Link() {
    if (Tail()) Tail()->next = this; else Head() = this;
    Tail() = this;
    Count()++;
  }
  // Raw value bits (float / int: the 4 bytes; bool: 0 / 1).
  uint32_t Bits() const {
    if (ty == 'b') return *(bool*)ptr ? 1u : 0u;
    uint32_t b;
    memcpy(&b, ptr, 4);
    return b;
  }
  // One store (main loop). float / int: a 32-bit word store; bool: a byte.
  void Store(uint32_t bits) {
    if (ty == 'b') *(volatile bool*)ptr = (bits != 0u);
    else *(volatile uint32_t*)ptr = bits;
    Gen() = Gen() + 1u;
  }
  void Reset() { Store(def); }
};

inline Node* Find(const char* name) {
  for (Node* n = Head(); n; n = n->next)
    if (!strcmp(n->name, name)) return n;
  return nullptr;
}

}  // namespace tunable

#define TUNABLE(type, name, value) \
  static type name = (value);      \
  static ::tunable::Node name##_tunable_node_(#name, &name)
#define TUNABLE_CONSTEXPR const
#define TUNABLE_CONSTEXPR_FN inline

#else

#define TUNABLE(type, name, value) constexpr type name = value
#define TUNABLE_CONSTEXPR constexpr
#define TUNABLE_CONSTEXPR_FN constexpr

#endif
