"""Attribute a quest_profile.py --simpleperf recording to functions.

  perf_symbols.py RUN_DIR/LABEL.perf.data UNSTRIPPED_LIBGEVR [--thread NAME]

simpleperf cannot read symbols for a library packed inside the APK, so this
reads raw samples (simpleperf_report_lib from the NDK), symbolizes libgevr
addresses with llvm-symbolizer against the archived unstripped library, and
prints self and inclusive (frame-pointer call chain) shares per function.
Other libraries are grouped by name (the GL driver has no symbols).
"""
import argparse
from collections import Counter
from pathlib import Path
import subprocess
import sys

NDK = Path('C:/Users/Occor/AppData/Local/Android/Sdk/ndk/25.1.8937393')
sys.path.insert(0, str(NDK / 'simpleperf'))
from simpleperf_report_lib import ReportLib  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('perf_data')
parser.add_argument('libgevr')
parser.add_argument('--thread', default='SDLThread')
parser.add_argument('--top', type=int, default=40)
args = parser.parse_args()

lib = ReportLib()
lib.SetRecordFile(args.perf_data)
samples = []
while True:
    sample = lib.GetNextSample()
    if sample is None:
        break
    if sample.thread_comm != args.thread:
        continue
    chain = [(lib.GetSymbolOfCurrentSample().dso_name, lib.GetSymbolOfCurrentSample().vaddr_in_file)]
    callchain = lib.GetCallChainOfCurrentSample()
    for i in range(callchain.nr):
        entry = callchain.entries[i]
        chain.append((entry.symbol.dso_name, entry.symbol.vaddr_in_file))
    samples.append(chain)
lib.Close()

addresses = sorted({a for chain in samples for dso, a in chain if dso.endswith('libgevr.so')})
symbolizer = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe'
out = subprocess.run([str(symbolizer), f'--obj={args.libgevr}', '--functions=short', '--no-inlines',
                      '--output-style=GNU'], input='\n'.join(hex(a) for a in addresses),
                     capture_output=True, text=True, check=True).stdout.splitlines()
names = {a: out[2 * i] for i, a in enumerate(addresses)}


def label(dso, addr):
    if dso.endswith('libgevr.so'):
        return names.get(addr, hex(addr))
    return '[' + dso.rsplit('/', 1)[-1].split('!')[-1] + ']'


self_count, incl_count = Counter(), Counter()
for chain in samples:
    labels = [label(d, a) for d, a in chain]
    self_count[labels[0]] += 1
    for name in set(labels):
        incl_count[name] += 1
total = len(samples)
print(f'{total} samples on {args.thread}')
print(f"{'self%':>6} {'incl%':>6}  function")
for name, n in self_count.most_common(args.top):
    print(f'{100 * n / total:6.2f} {100 * incl_count[name] / total:6.2f}  {name}')
# Who calls the (symbol-less) GL driver: the first libgevr frame above each driver sample.
driver = Counter()
for chain in samples:
    if 'adreno' in chain[0][0] or chain[0][0].endswith(('libgsl.so', 'libGLESv2.so')):
        ours = [label(d, a) for d, a in chain if d.endswith('libgevr.so')]
        driver[(ours[0] if ours else '?', ours[1] if len(ours) > 1 else '?')] += 1
print('\nGL driver time by calling function (and its caller):')
for (caller, parent), n in driver.most_common(args.top):
    print(f'{100 * n / total:6.2f}  {caller}  <- {parent}')
print('\nTop inclusive:')
for name, n in incl_count.most_common(args.top):
    print(f'{100 * self_count[name] / total:6.2f} {100 * n / total:6.2f}  {name}')
