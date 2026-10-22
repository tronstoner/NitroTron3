#pragma once
//
// Reads the comments of armitage_constants.h at startup so the armlive page
// can show them next to each tunable (never hand-copied text).
//
// For every `ARM_TUNABLE(type, NAME, ...)` / `ARM_FIXED(type, NAME, ...)` line:
//   section : the nearest preceding section header, a comment line of the form
//             `// --- Title ---` (leading "--- " and trailing dashes stripped).
//             Empty if the constant comes before the first header.
//   doc     : the comment block directly above the declaration: the contiguous
//             `//` lines up to (not including) a blank line, a code line or a
//             section header. Consecutive declarations with no comment between
//             them share the block above the first of them (a block documents
//             the run of constants under it); doc_of names the constant the
//             block sits directly above. Both empty if there is none.
//   note    : the trailing inline comment (text after `//` on the declaration
//             line), plus continuation lines directly below it whose comment
//             text itself starts with `//` (the `//    // ...` style used for
//             long notes); those lines are not part of the next doc block.
//             Empty if there is none.
// The `// ` prefix is removed from each line; further indentation is kept
// (tables in the comments stay aligned). Lines are joined with '\n'.

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace constdocs {

struct Entry {
  std::string section, doc, note;
  std::string doc_of;   // the constant the doc block sits directly above (itself, or the first of its run)
  int line = 0;
};

struct Docs {
  std::string path;                          // file actually read ("" = none)
  std::map<std::string, Entry> by_name;
  std::vector<std::string> order;            // declaration order in the file
};

inline std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

// "// text" -> "text" (one space after // removed, the rest of the indentation
// kept); returns false if the (trimmed) line is not a pure comment line.
inline bool CommentText(const std::string& line, std::string& out) {
  const std::string t = Trim(line);
  if (t.compare(0, 2, "//") != 0) return false;
  std::string r = t.substr(2);
  if (!r.empty() && r[0] == ' ') r.erase(0, 1);
  size_t e = r.size();
  while (e > 0 && isspace((unsigned char)r[e - 1])) e--;
  out = r.substr(0, e);
  return true;
}

// Section header: comment text starting with "---". Title = between the
// leading and trailing dash runs.
inline bool SectionTitle(const std::string& text, std::string& title) {
  if (text.compare(0, 3, "---") != 0) return false;
  size_t a = 0;
  while (a < text.size() && text[a] == '-') a++;
  size_t b = text.size();
  while (b > a && (text[b - 1] == '-' || isspace((unsigned char)text[b - 1]))) b--;
  title = Trim(text.substr(a, b - a));
  return true;
}

// Declaration line: optional whitespace, ARM_TUNABLE( or ARM_FIXED(, type, NAME.
inline bool DeclName(const std::string& line, std::string& name, std::string& rest_after_decl) {
  const std::string t = Trim(line);
  size_t p;
  if (t.compare(0, 12, "ARM_TUNABLE(") == 0) p = 12;
  else if (t.compare(0, 10, "ARM_FIXED(") == 0) p = 10;
  else return false;
  const size_t c1 = t.find(',', p);
  if (c1 == std::string::npos) return false;
  size_t a = c1 + 1;
  while (a < t.size() && isspace((unsigned char)t[a])) a++;
  size_t b = a;
  while (b < t.size() && (isalnum((unsigned char)t[b]) || t[b] == '_')) b++;
  if (b == a) return false;
  name = t.substr(a, b - a);
  rest_after_decl = t.substr(b);
  return true;
}

inline std::string Join(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) { if (i) s += '\n'; s += v[i]; }
  return s;
}

// Parses the file text. Never fails; unknown structure just yields empty fields.
inline void Parse(const std::string& text, Docs& d) {
  std::vector<std::string> lines;
  {
    size_t s = 0;
    while (s <= text.size()) {
      size_t e = text.find('\n', s);
      if (e == std::string::npos) e = text.size();
      std::string l = text.substr(s, e - s);
      if (!l.empty() && l.back() == '\r') l.pop_back();
      lines.push_back(l);
      s = e + 1;
    }
  }
  std::string section;
  std::vector<std::string> block;     // pending comment block (since the last break)
  std::string run_doc, run_of;        // doc of the current run of declarations, and its first constant
  bool in_run = false;                // previous non-continuation line was a declaration
  std::string last_decl;              // for continuation lines
  for (size_t i = 0; i < lines.size(); i++) {
    const std::string& line = lines[i];
    std::string name, rest, ct;
    if (DeclName(line, name, rest)) {
      Entry e;
      e.section = section;
      e.line = (int)i + 1;
      if (!block.empty()) { run_doc = Join(block); run_of = name; }
      else if (!in_run) { run_doc.clear(); run_of.clear(); }
      e.doc = run_doc;
      e.doc_of = run_doc.empty() ? std::string() : run_of;
      // inline comment: first "//" after the end of the declaration (the ';').
      const size_t semi = rest.find(';');
      const size_t from = semi == std::string::npos ? 0 : semi;
      const size_t cm = rest.find("//", from);
      if (cm != std::string::npos) e.note = Trim(rest.substr(cm + 2));
      if (!d.by_name.count(name)) d.order.push_back(name);
      d.by_name[name] = e;
      block.clear();
      in_run = true;
      last_decl = name;
      continue;
    }
    if (CommentText(line, ct)) {
      std::string title;
      if (SectionTitle(ct, title)) {
        section = title;
        block.clear();
        in_run = false;
        last_decl.clear();
        continue;
      }
      // continuation of the previous declaration's inline note: "//   // more"
      if (!last_decl.empty() && block.empty() && Trim(ct).compare(0, 2, "//") == 0) {
        std::string more = Trim(Trim(ct).substr(2));
        Entry& e = d.by_name[last_decl];
        e.note += (e.note.empty() ? "" : "\n") + more;
        continue;
      }
      block.push_back(ct);
      last_decl.clear();
      continue;
    }
    // blank or other code line: breaks blocks and runs
    block.clear();
    in_run = false;
    last_decl.clear();
  }
}

inline bool Load(const std::string& path, Docs& d) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::string s;
  char b[65536];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
  fclose(f);
  d = Docs();
  char real[PATH_MAX];
  d.path = realpath(path.c_str(), real) ? std::string(real) : path;   // shown on the page
  Parse(s, d);
  return true;
}

}  // namespace constdocs
