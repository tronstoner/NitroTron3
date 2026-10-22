#pragma once
// Tiny JSON reader/writer helpers for armlive's control protocol (server thread
// only; allocates freely). Numbers keep their source text (`raw`) so a float
// constant sent as 0.18 is parsed with strtof exactly like the source literal.
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
  enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0.0;
  std::string s;    // string value, or the raw text of a number
  std::vector<Value> a;
  std::vector<std::pair<std::string, Value>> o;
};

class Parser {
 public:
  explicit Parser(const std::string& src) : p_(src.c_str()), e_(src.c_str() + src.size()) {}
  bool Parse(Value& v, std::string& err) {
    if (!Val(v)) { err = err_.empty() ? "bad JSON" : err_; return false; }
    Ws();
    if (p_ != e_) { err = "trailing characters after JSON"; return false; }
    return true;
  }

 private:
  const char *p_, *e_;
  std::string err_;
  int depth_ = 0;

  void Ws() { while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) p_++; }
  bool Lit(const char* w) {
    const size_t n = strlen(w);
    if ((size_t)(e_ - p_) < n || strncmp(p_, w, n)) return false;
    p_ += n;
    return true;
  }
  bool Val(Value& v) {
    Ws();
    if (p_ >= e_) return false;
    if (++depth_ > 32) { err_ = "JSON nested too deep"; return false; }
    bool ok = false;
    const char c = *p_;
    if (c == '{') ok = ObjV(v);
    else if (c == '[') ok = ArrV(v);
    else if (c == '"') { v.t = Value::Str; ok = StrV(v.s); }
    else if (Lit("true")) { v.t = Value::Bool; v.b = true; ok = true; }
    else if (Lit("false")) { v.t = Value::Bool; v.b = false; ok = true; }
    else if (Lit("null")) { v.t = Value::Null; ok = true; }
    else ok = NumV(v);
    depth_--;
    return ok;
  }
  bool NumV(Value& v) {
    const char* s = p_;
    if (p_ < e_ && (*p_ == '-' || *p_ == '+')) p_++;
    while (p_ < e_ && (isdigit((unsigned char)*p_) || *p_ == '.' || *p_ == 'e' || *p_ == 'E' || *p_ == '-' || *p_ == '+')) p_++;
    if (p_ == s) { err_ = "unexpected character in JSON"; return false; }
    v.t = Value::Num;
    v.s.assign(s, p_);
    char* end = nullptr;
    v.n = strtod(v.s.c_str(), &end);
    if (end != v.s.c_str() + v.s.size()) { err_ = "bad number " + v.s; return false; }
    return true;
  }
  bool StrV(std::string& out) {
    p_++;   // opening quote
    while (p_ < e_ && *p_ != '"') {
      if (*p_ == '\\') {
        if (++p_ >= e_) return false;
        switch (*p_) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {   // BMP only, enough for this protocol
            if (e_ - p_ < 5) return false;
            const unsigned cp = (unsigned)strtoul(std::string(p_ + 1, 4).c_str(), nullptr, 16);
            p_ += 4;
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); }
            break;
          }
          default: out += *p_; break;
        }
        p_++;
      } else {
        out += *p_++;
      }
    }
    if (p_ >= e_) { err_ = "unterminated string"; return false; }
    p_++;
    return true;
  }
  bool ArrV(Value& v) {
    v.t = Value::Arr;
    p_++;
    Ws();
    if (p_ < e_ && *p_ == ']') { p_++; return true; }
    for (;;) {
      Value x;
      if (!Val(x)) return false;
      v.a.push_back(std::move(x));
      Ws();
      if (p_ < e_ && *p_ == ',') { p_++; continue; }
      if (p_ < e_ && *p_ == ']') { p_++; return true; }
      err_ = "expected , or ] in array";
      return false;
    }
  }
  bool ObjV(Value& v) {
    v.t = Value::Obj;
    p_++;
    Ws();
    if (p_ < e_ && *p_ == '}') { p_++; return true; }
    for (;;) {
      Ws();
      if (p_ >= e_ || *p_ != '"') { err_ = "expected key string"; return false; }
      std::string k;
      if (!StrV(k)) return false;
      Ws();
      if (p_ >= e_ || *p_ != ':') { err_ = "expected :"; return false; }
      p_++;
      Value x;
      if (!Val(x)) return false;
      v.o.emplace_back(std::move(k), std::move(x));
      Ws();
      if (p_ < e_ && *p_ == ',') { p_++; continue; }
      if (p_ < e_ && *p_ == '}') { p_++; return true; }
      err_ = "expected , or } in object";
      return false;
    }
  }
};

inline std::string Esc(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}

// Number for JSON output: finite -> %.6g, otherwise null.
inline std::string Num(double v, int prec = 6) {
  if (!std::isfinite(v)) return "null";
  char b[48];
  snprintf(b, sizeof b, "%.*g", prec, v);
  return b;
}

}  // namespace json
