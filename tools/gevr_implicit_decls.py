#!/usr/bin/env python3
"""
Find functions the game calls without a prototype whose real return type is
not int-compatible: a pointer, a type narrower than int, or a float.

Why this matters on a 64-bit host
---------------------------------
C gives an undeclared function the return type int. On the N64 that was
harmless - a pointer was 32 bits, the same as an int - so the decomp never
needed every prototype in scope. Here a pointer is 64 bits, and a call through
an implicit declaration keeps only the low half of it. The damage then surfaces
somewhere else entirely, as a fault on a truncated address.

Narrow and float returns break the same way for a different reason. The MIPS
callee returned an s8/u8/s16/u16 already extended to the full register, so an
implicit int read it correctly; on AArch64 the caller does the extending and
the upper bits are garbage (joyGetStickY() came back as 176 for -80, so every
down or left push on the watch read as up or right). A float comes back in a
float register the caller never looks at.

The compiler reports every such call ("implicit declaration of function"),
but most of them return int and are harmless. This cross-references each one
with its definition and keeps the ones that return a pointer, along with the
header that declares them and the files that call them blind, so the fix is a
matter of adding the right #include.

Usage
-----
    python tools/gevr_implicit_decls.py <warnings.txt>

where warnings.txt is a full rebuild's compiler output.
"""

import os
import re
import sys
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

WARN_RE = re.compile(r"^(.*?\.c):(\d+):\d+: warning: implicit declaration of function '(\w+)'")
# A definition: a return type that ends in '*', then the name, then '('.
# Anchored at column 0, which is how the decomp writes every definition.
DEF_RE_TMPL = r"^((?:const\s+)?(?:struct\s+|union\s+|enum\s+)?\w+[\w\s]*?\**)\s*\b%s\s*\("
# Return types an implicit int declaration mangles on this host (pointers aside).
NARROW = {"s8", "u8", "s16", "u16", "char", "signed char", "unsigned char", "short",
          "unsigned short", "bool", "_Bool", "s64", "u64", "f32", "f64", "float", "double"}


def needs_proto(rettype):
    """True for a return type an implicit int declaration would mangle."""
    t = " ".join(rettype.replace("const ", "").split())
    if t.endswith("*"):
        return True
    return t in NARROW
DECL_RE_TMPL = r"^\s*(?:extern\s+)?[\w\s\*]+?\b%s\s*\([^;]*\)\s*;"


def load_sources(pattern):
    out = {}
    for path in glob.glob(os.path.join(REPO, pattern), recursive=True):
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                out[os.path.relpath(path, REPO).replace("\\", "/")] = f.read()
        except OSError:
            pass
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    uses = {}
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for line in f:
            m = WARN_RE.search(line.strip())
            if m:
                path = m.group(1).replace("\\", "/")
                # The build runs from the .cxx directory; keep the tail that is inside the repo.
                root = REPO.replace("\\", "/").rstrip("/") + "/"
                if path.lower().startswith(root.lower()):
                    path = path[len(root):]
                uses.setdefault(m.group(3), set()).add(path)

    sources = {}
    sources.update(load_sources("src/**/*.c"))
    sources.update(load_sources("port/src/**/*.c"))
    headers = {}
    headers.update(load_sources("src/**/*.h"))
    headers.update(load_sources("include/**/*.h"))
    headers.update(load_sources("port/include/**/*.h"))

    found = []
    for name in sorted(uses):
        def_re = re.compile(DEF_RE_TMPL % re.escape(name), re.M)
        rettype = None
        deffile = None
        for path, text in sources.items():
            for m in def_re.finditer(text):
                # a definition, not a prototype: the parameter list is followed by '{'
                j, depth = m.end() - 1, 0
                while j < len(text):
                    if text[j] == "(":
                        depth += 1
                    elif text[j] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                if text[j + 1:].lstrip()[:1] != "{":
                    continue
                if m.group(1).split()[0] == "static":
                    continue
                rettype = " ".join(m.group(1).split())
                deffile = path
                break
            if rettype:
                break
        if not rettype or not needs_proto(rettype):
            continue

        decl_re = re.compile(DECL_RE_TMPL % re.escape(name), re.M)
        declared_in = [p for p, t in headers.items() if decl_re.search(t)]
        found.append((name, rettype, deffile, declared_in, sorted(uses[name])))

    print("%d functions called without a prototype; %d of them return a pointer, narrow or float type:\n" % (len(uses), len(found)))
    for name, rettype, deffile, declared_in, callers in found:
        print("%s  ->  %s   (defined in %s)" % (name, rettype, deffile))
        print("    declared in: %s" % (", ".join(declared_in) if declared_in else "NO HEADER"))
        print("    called blind from: %s" % ", ".join(callers))

    if len(sys.argv) > 2:
        emit_header(sys.argv[2], found, sources)
    return 0


def signature(name, deffile, sources):
    """The definition's full signature, from return type through the closing paren."""
    text = sources[deffile]
    m = re.search(DEF_RE_TMPL % re.escape(name), text, re.M)
    start = m.start()
    depth = 0
    i = m.end() - 1  # at '('
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    sig = text[start:i + 1]
    sig = re.sub(r"/\*.*?\*/", "", sig, flags=re.S)
    sig = re.sub(r"//[^\n]*", "", sig)
    return " ".join(sig.split())


def emit_header(path, found, sources):
    """
    One header carrying the exact prototype of every pointer-returning function
    that some file calls blind. bondtypes.h includes it, so every game file that
    knows the engine's types also knows these return pointers.
    """
    lines = []
    lines.append("/*")
    lines.append(" * GENERATED by tools/gevr_implicit_decls.py - do not edit.")
    lines.append(" *")
    lines.append(" * Prototypes for functions that at least one file calls without a declaration")
    lines.append(" * in scope and whose return type an implicit int mangles on this host:")
    lines.append(" *  - pointers: the call kept the low 32 bits of the address and faulted far away;")
    lines.append(" *  - s8/u8/s16/u16: MIPS callees extended these to the full register, AArch64")
    lines.append(" *    callers do it themselves, so the implicit int saw garbage upper bits")
    lines.append(" *    (joyGetStickY() read -80 as 176);")
    lines.append(" *  - f32/f64: returned in a float register the caller never read.")
    lines.append(" * Each is copied from the definition, with the file that defines it and the")
    lines.append(" * files that called it blind.")
    lines.append(" */")
    lines.append("#ifndef _GEVR_IMPLICIT_PROTOS_H_")
    lines.append("#define _GEVR_IMPLICIT_PROTOS_H_")
    lines.append("")
    entries = {}
    # Keep what the header already carries: those calls no longer warn, because
    # of this header, so a fresh build's warnings cannot rediscover them.
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            old = f.read()
        for m in re.finditer(r"^/\* (\S+); called blind from (.*?) \*/\n(.*?;)\n", old, re.M):
            nm = re.search(r"(\w+)\s*\(", m.group(3)).group(1)
            entries[nm] = (m.group(1), m.group(2), m.group(3))
    for name, rettype, deffile, declared_in, callers in found:
        entries[name] = (deffile, ", ".join(callers), signature(name, deffile, sources) + ";")
    for name in sorted(entries, key=lambda n: (entries[n][0], n)):
        deffile, callers, proto = entries[name]
        lines.append("/* %s; called blind from %s */" % (deffile, callers))
        lines.append(proto)
    lines.append("")
    lines.append("#endif")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("\nwrote %s (%d prototypes)" % (path, len(entries)))


if __name__ == "__main__":
    sys.exit(main())
