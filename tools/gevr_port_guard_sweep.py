import re, pathlib, collections, json, os, sys

REF = pathlib.Path(r'C:\Users\Occor\Documents\other_projects\gepc-ref')
OURS = pathlib.Path(r'C:\Users\Occor\Documents\other_projects\goldeneye-vr')


def sources(root, subs):
    out = []
    for sub in subs:
        d = root / sub
        if not d.exists():
            continue
        for ext in ('*.c', '*.h', '*.cpp'):
            out += list(d.rglob(ext))
    return out


open_re = re.compile(r'^\s*#\s*(ifdef\s+PORT|if\s+defined\s*\(\s*PORT\s*\)|if\s+defined\s+PORT)\b')
any_if = re.compile(r'^\s*#\s*if')
endif_re = re.compile(r'^\s*#\s*endif')
find_re = re.compile(r'\b(D\d{1,3}|RC\d|M-\d+[a-z]?)\b')

sites = []
for f in sources(REF, ('src', 'port')):
    try:
        lines = f.read_text(errors='replace').splitlines()
    except Exception:
        continue
    i = 0
    while i < len(lines):
        if open_re.match(lines[i]):
            depth, j, body = 1, i + 1, []
            while j < len(lines) and depth:
                if any_if.match(lines[j]):
                    depth += 1
                elif endif_re.match(lines[j]):
                    depth -= 1
                if depth:
                    body.append(lines[j])
                j += 1
            ctx = '\n'.join(lines[max(0, i - 6):i] + body)
            ids = sorted(set(find_re.findall(ctx)))
            rel = str(f.relative_to(REF)).replace(os.sep, '/')
            sites.append((rel, i + 1, ids, '\n'.join(body)))
            i = j
        else:
            i += 1

ours_blob = []
for f in sources(OURS, ('src', 'port', 'assets', 'docs')):
    try:
        ours_blob.append(f.read_text(errors='replace'))
    except Exception:
        pass
for extra in ('HANDOFF.md', 'STATUS.md'):
    p = OURS / extra
    if p.exists():
        ours_blob.append(p.read_text(errors='replace'))
ours_ids = set(find_re.findall('\n'.join(ours_blob)))

byid = collections.defaultdict(list)
for path, line, ids, body in sites:
    for i in ids:
        byid[i].append((path, line))

missing = {i: locs for i, locs in byid.items() if i not in ours_ids}
present = {i: locs for i, locs in byid.items() if i in ours_ids}


def numkey(s):
    return (s[0], int(re.sub(r'\D', '', s) or 0))


print('PORT guard sites in gepc-ref : %d' % len(sites))
print('distinct finding ids         : %d' % len(byid))
print('  cited somewhere in our tree: %d' % len(present))
print('  NOT cited in our tree      : %d' % len(missing))
print()
print('=== uncited findings, by reference file (most first) ===')
perfile = collections.defaultdict(set)
for i, locs in missing.items():
    for path, line in locs:
        perfile[path].add(i)
for path, ids in sorted(perfile.items(), key=lambda x: (-len(x[1]), x[0])):
    print('%-32s %3d  %s' % (path, len(ids), ' '.join(sorted(ids, key=numkey))))

out = {
    'missing': {k: v for k, v in missing.items()},
    'present': sorted(present.keys(), key=numkey),
    'sites': [(p, l, i) for p, l, i, _ in sites],
}
json.dump(out, open(OURS / 'scratchpad' / 'port_guard_sweep.json', 'w'), indent=1)
print()
print('wrote scratchpad/port_guard_sweep.json')
