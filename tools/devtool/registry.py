"""Live-constant registry from the constants files (docs/DEV_INTERFACE.md section 4).

Python port of tools/hostsim/const_docs.h, plus the `@` tags of
src/core/util/tunable.h. The constants files are the single source of truth;
nothing here is hand-kept.

Per `TUNABLE(type, NAME, value);` line:
  section     nearest preceding `// --- Title ---` header (dashes stripped)
  doc         the comment block directly above the declaration (contiguous `//`
              lines up to a blank line, a code line or a section header).
              Consecutive declarations (TUNABLE or plain constexpr) with no
              comment between them share the block above the first of them;
              doc_of names that first constant.
  note        trailing inline comment after the `@` tags, plus continuation
              lines below whose comment text itself starts with `//`.
  tags        @unit <u>  @range <min> <max>  @taper lin|log  @level live|reactivate
              @pair <ID> <end>   (optional; see PAIRS below)
              (written first in the inline comment, before the note text)
  label       readable label from the note's first clause (parentheses and a
              leading unit like `Hz;` removed), else the doc's first sentence,
              else the name.

PAIRS: constants that are the ends of one law share `@pair <ID> <end>` and are
shown as ONE row (one multi-handle slider in their common unit / taper).
  ID   the row label, `_` = space (e.g. `Pluck_decay_time`)
  end  where the constant acts: knob position `ccw`, `noon`, `cw` (a knob-end
       pair: the values the knob law reaches there), or the ends of a value range
       that is not a knob law: `floor` / `top`, `fast` / `slow` (a "range" row).
  A pair needs >= 2 members, distinct ends, one kind (knob or range), the same
  type / unit / taper; members are ordered by END_ORDER.

Python 3 stdlib only.
"""
import os
import re

SECTION_RE = re.compile(r"^-{3,}")
TUNABLE_RE = re.compile(r"^\s*TUNABLE\(\s*(float|int|bool)\s*,\s*(\w+)\s*,\s*(.*?)\s*\)\s*;(.*)$")
CONSTEXPR_RE = re.compile(r"^\s*(?:static\s+)?constexpr\s+[\w:]+\s+(\w+)\b(.*)$")
TAG_RE = re.compile(r"@(\w+)((?:\s+(?!@)\S+)*)")
LABEL_UNITS = {"hz", "s", "ms", "db", "dbfs", "bits", "oct", "rad", "x", "t4 units"}
LEVELS = ("live", "reactivate")
END_ORDER = ("ccw", "noon", "cw", "floor", "top", "fast", "slow")
END_LABELS = {"ccw": "CCW", "noon": "noon", "cw": "CW", "floor": "floor", "top": "top",
              "fast": "fastest env", "slow": "slowest env"}
KNOB_ENDS = ("ccw", "noon", "cw")
TAPERS = ("lin", "log")
LABEL_MAX = 64


def comment_text(line):
    """'  // text' -> 'text' (one space after // removed); None if not a pure comment line."""
    t = line.strip()
    if not t.startswith("//"):
        return None
    r = t[2:]
    if r.startswith(" "):
        r = r[1:]
    return r.rstrip()


def section_title(text):
    if not SECTION_RE.match(text):
        return None
    return text.strip().strip("-").strip()


def parse_tags(comment):
    """'@unit Hz @range 1 20 @taper log  note' -> ({unit: 'Hz', range: [1, 20], ...}, 'note')."""
    tags = {}
    rest = comment.strip()
    while rest.startswith("@"):
        m = TAG_RE.match(rest)
        if not m:
            break
        key, vals = m.group(1), m.group(2).split()
        # a tag takes a fixed number of values; anything after is note text
        n = 2 if key in ("range", "pair") else 1
        tags[key] = vals[:n]
        tail = vals[n:]
        rest = (" ".join(tail) + rest[m.end():]).strip() if tail else rest[m.end():].strip()
    return tags, rest


def strip_parens(s):
    out, depth = [], 0
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def make_label(name, note, doc, unit):
    def clean(s):
        s = strip_parens(s)
        s = re.sub(r"\s+", " ", s).strip()
        s = re.sub(r"\s+([,;:])", r"\1", s)
        m = re.match(r"^([\w ]{1,10}?)\s*[;:]\s*(.*)$", s)
        if m and (m.group(1).strip().lower() in LABEL_UNITS
                  or (unit and m.group(1).strip().lower() == unit.lower())):
            s = m.group(2)
        return s.split(";")[0].strip().rstrip(",:")

    cands = [clean(note.split("\n")[0]) if note else ""]
    if doc:
        cands.append(re.split(r"(?<=[a-z0-9])\.\s", clean(doc.split("\n")[0]))[0])
    for s in cands:
        if len(s) >= 3:
            if len(s) > LABEL_MAX:
                s = s[:LABEL_MAX].rsplit(" ", 1)[0].rstrip(",;:") + "\u2026"
            if s[0].islower() and not (len(s) > 1 and s[1].isupper()):   # keep "dB", "t4"
                s = s[0].upper() + s[1:]
            return s
    return name


def num(text, ty):
    t = text.strip()
    if ty == "bool":
        if t in ("true", "false"):
            return t == "true"
        raise ValueError(text)
    t = re.sub(r"(?<=[0-9.])[fF]$", "", t)
    v = float(t)
    return int(v) if ty == "int" else v


def parse_file(path, rel=None):
    """Returns (entries, problems). entries: list of dicts in file order."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    rel = rel or path
    module = re.sub(r"_constants$", "", os.path.splitext(os.path.basename(path))[0])
    entries, problems = [], []
    section = ""
    block = []
    run_doc, run_of = "", ""
    in_run = False
    last = None       # entry that may take note continuation lines
    for i, raw in enumerate(lines):
        line = raw.rstrip("\r")
        m = TUNABLE_RE.match(line)
        cm = None if m else CONSTEXPR_RE.match(line)
        if m or cm:
            name = m.group(2) if m else cm.group(1)
            if block:
                run_doc, run_of = "\n".join(block), name
            elif not in_run:
                run_doc, run_of = "", ""
            block = []
            in_run = True
            last = None
            if not m:
                continue
            ty, value, tail = m.group(1), m.group(3), m.group(4)
            ci = tail.find("//")
            comment = tail[ci + 2:].strip() if ci >= 0 else ""
            tags, note = parse_tags(comment)
            e = {"name": name, "type": ty, "default_text": value, "module": module,
                 "section": section, "doc": run_doc, "doc_of": run_of if run_doc else "",
                 "note": note, "file": rel, "line": i + 1}
            try:
                e["default"] = num(value, ty)
            except ValueError:
                e["default"] = None
                problems.append("%s:%d %s: default %r is not a plain number" % (rel, i + 1, name, value))
            e["unit"] = (tags.get("unit") or [""])[0]
            rng = tags.get("range")
            e["range"] = None
            if ty != "bool":
                try:
                    lo, hi = float(rng[0]), float(rng[1])
                    if not lo < hi:
                        raise ValueError
                    e["range"] = [lo, hi]
                except (TypeError, ValueError, IndexError):
                    problems.append("%s:%d %s: missing / bad @range" % (rel, i + 1, name))
            taper = (tags.get("taper") or ["lin"])[0]
            if taper not in TAPERS or (taper == "log" and e["range"] and e["range"][0] <= 0):
                problems.append("%s:%d %s: bad @taper %s" % (rel, i + 1, name, taper))
                taper = "lin"
            e["taper"] = taper
            level = (tags.get("level") or [""])[0]
            if level not in LEVELS:
                problems.append("%s:%d %s: missing / bad @level" % (rel, i + 1, name))
                level = "live"
            e["level"] = level
            e["when"] = (" ".join(tags.get("when") or [])) or None
            e["pair"] = None
            if "pair" in tags:
                pv = tags["pair"]
                if len(pv) == 2 and re.match(r"^\w+$", pv[0]) and pv[1] in END_ORDER:
                    e["pair"] = {"id": pv[0], "end": pv[1], "end_label": END_LABELS[pv[1]]}
                else:
                    problems.append("%s:%d %s: bad @pair (want: @pair <ID> %s)" % (rel, i + 1, name, "|".join(END_ORDER)))
            if e["default"] is not None and e["range"] and ty != "bool":
                if not e["range"][0] <= e["default"] <= e["range"][1]:
                    problems.append("%s:%d %s: default outside @range" % (rel, i + 1, name))
            e["label"] = make_label(name, note, run_doc, e["unit"])
            entries.append(e)
            last = e
            continue
        ct = comment_text(line)
        if ct is not None:
            title = section_title(ct)
            if title is not None:
                section = title
                block, in_run, last = [], False, None
                continue
            # note continuation: comment text starting with `//`, or a comment indented far
            # right (aligned under the inline note) directly below the declaration
            indent = len(line) - len(line.lstrip())
            if last is not None and not block and (ct.strip().startswith("//") or indent >= 16):
                more = ct.strip()[2:].strip() if ct.strip().startswith("//") else ct.strip()
                last["note"] += ("\n" if last["note"] else "") + more
                continue
            block.append(ct)
            last = None
            continue
        # blank or other code line (array body, closing brace): ends blocks and runs
        block, in_run, last = [], False, None
    return entries, problems


def constants_files(repo, sources):
    """sources: dirs / files relative to repo -> every .h with a TUNABLE( declaration."""
    out = []
    for src in sources:
        p = os.path.join(repo, src)
        cands = []
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for fn in sorted(files):
                    if fn.endswith(".h"):
                        cands.append(os.path.join(root, fn))
        elif os.path.isfile(p):
            cands.append(p)
        for c in sorted(cands):
            try:
                with open(c, encoding="utf-8", errors="replace") as f:
                    if re.search(r"^\s*TUNABLE\(", f.read(), re.M) and c not in out:
                        out.append(c)
            except OSError:
                pass
    return out


def build(repo, pedal, sources):
    """Registry for one pedal: {pedal, files, constants: [...], problems: [...]}."""
    files = constants_files(repo, sources)
    consts, problems = [], []
    for p in files:
        rel = os.path.relpath(p, repo)
        e, pr = parse_file(p, rel)
        for x in e:
            x["pedal"] = pedal
        consts.extend(e)
        problems.extend(pr)
    seen = set()
    for c in consts:
        if c["name"] in seen:
            problems.append("%s: declared twice" % c["name"])
        seen.add(c["name"])
    return {"pedal": pedal, "files": [os.path.relpath(p, repo) for p in files],
            "constants": consts, "pairs": build_pairs(consts, problems), "problems": problems}


def build_pairs(consts, problems):
    """{ID: {label, kind: knob|range, members: [names, END_ORDER]}}; a bad pair is reported and dropped
    (its members then show as single rows)."""
    groups = {}
    for c in consts:
        if c.get("pair"):
            groups.setdefault(c["pair"]["id"], []).append(c)
    pairs = {}
    for pid, mem in groups.items():
        ends = [c["pair"]["end"] for c in mem]
        knob = [e in KNOB_ENDS for e in ends]
        why = None
        if len(mem) < 2:
            why = "only one member"
        elif len(set(ends)) != len(ends):
            why = "an end is used twice"
        elif any(knob) and not all(knob):
            why = "knob ends (ccw / noon / cw) mixed with range ends"
        elif len({(c["type"], c["unit"], c["taper"]) for c in mem}) != 1:
            why = "members differ in type / unit / taper"
        elif any(c["type"] == "bool" or not c["range"] for c in mem):
            why = "members need a number type and a @range"
        if why:
            problems.append("@pair %s: %s" % (pid, why))
            for c in mem:
                c["pair"] = None
            continue
        mem.sort(key=lambda c: END_ORDER.index(c["pair"]["end"]))
        pairs[pid] = {"label": pid.replace("_", " "), "kind": "knob" if all(knob) else "range",
                      "members": [c["name"] for c in mem]}
    return pairs
