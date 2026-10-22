#pragma once
//
// Host-only tunable layer for pedals/nitrolab/modules/armitage_constants.h.
//
// The constants file declares every constant as ARM_TUNABLE(type, NAME, value)
// or ARM_FIXED(type, NAME, value). In the firmware both expand to
// `constexpr type NAME = value`. This header defines ARM_TUNABLE BEFORE the
// constants file is included, so on the host each tunable becomes an
// `inline type NAME` runtime variable whose initial value is the same default
// expression, registered in a table (name, type, default text, pointer).
// ARM_FIXED stays a real constexpr (array sizes, the sin-table mask) and is
// registered read-only.
//
// The default is kept as a thunk, not a number: after --set overrides,
// ApplyDefaults() re-evaluates the default of every tunable that was NOT set,
// in declaration order — so derived defaults follow their inputs (e.g.
// ARM_GAIN_UP = ARM_ALIGN ? 1.139f : 0.30f follows `--set ARM_ALIGN=0`).
//
// armitage.h itself is compiled unmodified. It uses a few constants inside
// constexpr expressions (DeltaGain(), kDenFloor, the soft clipper's kIn/kOut,
// one static_assert); for those, the include of armitage.h is wrapped (see
// armitage_host.h): `constexpr` reads as `const` and
// `static_assert` is dropped while armitage.h is parsed. The dropped
// static_assert is checked at runtime instead (ValidateTunables()).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace hostsim {

enum class Ty { F32, I32, BOOL, U32 };

struct Tunable {
  const char* name;
  Ty          ty;
  void*       ptr;        // nullptr for ARM_FIXED
  double    (*def)();     // default (re-evaluated, see ApplyDefaults)
  const char* def_text;   // the default expression as written in armitage_constants.h
  bool        fixed;      // compile-time constant on the host too (not settable)
  double      fixed_val;
  bool        set;        // overridden on the command line
};

inline std::vector<Tunable>& Registry() { static std::vector<Tunable> r; return r; }

template <class T> constexpr Ty TyOf();
template <> constexpr Ty TyOf<float>()    { return Ty::F32; }
template <> constexpr Ty TyOf<int>()      { return Ty::I32; }
template <> constexpr Ty TyOf<bool>()     { return Ty::BOOL; }
template <> constexpr Ty TyOf<uint32_t>() { return Ty::U32; }

template <class T>
T Reg(const char* name, T* p, double (*d)(), const char* text) {
  Registry().push_back({name, TyOf<T>(), p, d, text, false, 0.0, false});
  return (T)d();
}
template <class T>
bool RegFixed(const char* name, T v, const char* text) {
  Registry().push_back({name, TyOf<T>(), nullptr, nullptr, text, true, (double)v, false});
  return true;
}

inline double Get(const Tunable& t) {
  if (t.fixed) return t.fixed_val;
  switch (t.ty) {
    case Ty::F32:  return *(float*)t.ptr;
    case Ty::I32:  return *(int*)t.ptr;
    case Ty::BOOL: return *(bool*)t.ptr ? 1.0 : 0.0;
    case Ty::U32:  return *(uint32_t*)t.ptr;
  }
  return 0.0;
}
inline void Put(const Tunable& t, double v) {
  switch (t.ty) {
    case Ty::F32:  *(float*)t.ptr = (float)v; break;
    case Ty::I32:  *(int*)t.ptr = (int)v; break;
    case Ty::BOOL: *(bool*)t.ptr = (v != 0.0); break;
    case Ty::U32:  *(uint32_t*)t.ptr = (uint32_t)v; break;
  }
}
inline const char* TyName(Ty t) {
  switch (t) { case Ty::F32: return "float"; case Ty::I32: return "int";
               case Ty::BOOL: return "bool"; case Ty::U32: return "uint32_t"; }
  return "?";
}
inline Tunable* Find(const char* name) {
  for (auto& t : Registry()) if (!strcmp(t.name, name)) return &t;
  return nullptr;
}

// "NAME=value". Returns an error message, or empty on success.
inline std::string SetFromString(const char* spec) {
  const char* eq = strchr(spec, '=');
  if (!eq) return std::string("--set expects NAME=value: ") + spec;
  std::string name(spec, eq - spec);
  const char* val = eq + 1;
  Tunable* t = Find(name.c_str());
  if (!t) return "unknown constant: " + name + " (see --dump-constants)";
  if (t->fixed) return name + " is fixed at compile time (array size / table mask), not settable";
  double v;
  if (t->ty == Ty::BOOL && (!strcmp(val, "true") || !strcmp(val, "false"))) {
    v = !strcmp(val, "true") ? 1.0 : 0.0;
  } else {
    char* end = nullptr;
    v = strtod(val, &end);
    if (end == val || *end) return "bad value for " + name + ": " + val;
  }
  // float: parse via strtof so `--set X=0.18` gives exactly 0.18f, like the source literal
  if (t->ty == Ty::F32) *(float*)t->ptr = strtof(val, nullptr);
  else Put(*t, v);
  t->set = true;
  return "";
}

// Re-evaluate the default of every tunable not set on the command line, in
// declaration order (derived defaults follow their inputs).
inline void ApplyDefaults() {
  for (auto& t : Registry())
    if (!t.fixed && !t.set) Put(t, t.def());
}

inline void Dump(FILE* f) {
  fprintf(f, "# name\ttype\tvalue\tdefault-expression\t[fixed|set]\n");
  for (auto& t : Registry()) {
    const double v = Get(t);
    char buf[64];
    if (t.ty == Ty::F32) {                       // shortest text that reads back as the same float
      for (int prec = 6; prec <= 9; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (strtof(buf, nullptr) == (float)v) break;
      }
    }
    else if (t.ty == Ty::BOOL) snprintf(buf, sizeof buf, "%s", v != 0.0 ? "true" : "false");
    else snprintf(buf, sizeof buf, "%.0f", v);
    fprintf(f, "%s\t%s\t%s\t%s%s\n", t.name, TyName(t.ty), buf, t.def_text,
            t.fixed ? "\tfixed" : (t.set ? "\tset" : ""));
  }
}

}  // namespace hostsim

#define ARM_TUNABLE(type, name, ...)                                              \
  inline type name = ::hostsim::Reg<type>(#name, &name,                          \
      +[]() -> double { return (double)(__VA_ARGS__); }, #__VA_ARGS__)
#define ARM_FIXED(type, name, ...)                                                \
  constexpr type name = __VA_ARGS__;                                              \
  inline const bool name##_hostsim_reg_ = ::hostsim::RegFixed<type>(#name, name, #__VA_ARGS__)
