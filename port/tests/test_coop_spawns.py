"""Exercise actual reinforcement AI, spawn packets and puppets in four stub worlds."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from test_reinforcements import block

ROOT = Path(__file__).resolve().parents[2]
coop = (ROOT / "port/src/net/net_coop.c").read_text(encoding="utf-8")
action = (ROOT / "src/game/chraction.c").read_text(encoding="utf-8")
chr_source = (ROOT / "src/game/chr.c").read_text(encoding="utf-8")
ai = (ROOT / "src/game/chrai.c").read_text(encoding="utf-8")
fixture = (ROOT / "port/tests/coop_spawns_native.c").read_text(encoding="utf-8")

types = re.search(r"#define COOP_MAX_SLOTS \d+", coop)[0] + "\n"
for member, last in (("    NetChrState st;\n    u64 sent_us;", "CoopSent"),
                     ("    NetChrState st;\n    u32 seq;", "CoopPuppet"),
                     ("    u16 hostslot;\n    s16 chrnum;", "CoopSpawn")):
    start = coop.rfind("typedef struct {", 0, coop.index(member))
    end = coop.index("} " + last + ";", start) + len("} " + last + ";")
    types += coop[start:end] + "\n"
types += re.search(r"#define COOP_PENDING_SPAWNS \w+", coop)[0] + "\n"
types += "\n".join(re.search(r"#define " + name + r" .*", coop)[0] for name in
                   ("COOP_SEND_INTERVAL_US", "COOP_REFRESH_US"))
functions = (
    "static s32 coopSlotOf(", "static int coopChrLive(", "static void coopCollect(",
    "static int coopChanged(", "static void coopHeader(struct netbuf *b, u8 type)\n{",
    "static void coopWriteSpawn(struct netbuf *buf, ChrRecord *chr, s32 slot, AIRecord *ailist, s32 spawnflags)\n{",
    "void gevrCoopChrSpawned(", "void gevrCoopChrIdentityChanged(",
    "int netCoopHostSlotOf(", "static ChrRecord *coopLocalChr(", "static void coopReceiveStates(",
    "static int coopMakeSpawn(", "static void coopForgetPendingSpawn(", "static void coopRetrySpawns(",
    "static void coopReceiveSpawn(", "void netCoopHostTick(", "void netCoopPuppetTick(",
)
parts = {
    "TYPES": types,
    "NETWORK": "\n\n".join(block(coop, name) for name in functions),
    "LOOKUPS": "\n\n".join((block(chr_source, "ChrRecord* chrFindByLiteralId("),
                              block(action, "s32 chrResolveId("), block(action, "ChrRecord *chrFindById("),
                              block(action, "s32 gevrFastReinforcements("))),
    "AI_EXISTENCE": block(ai, "case AI_IFChrDoesNotExist:\n                {"),
    "AI_CLONE": block(ai, "case AI_TRYCloningChr:\n                {"),
}
for marker, text in parts.items():
    fixture = fixture.replace("/* INSERT_" + marker + " */", text)
with tempfile.TemporaryDirectory(prefix="gevr-coop-spawns-") as temp:
    source, exe = Path(temp) / "test.c", Path(temp) / "test.exe"
    source.write_text(fixture, encoding="utf-8")
    args = [shutil.which("gcc") or "gcc", "-std=c11", "-O2", "-fms-extensions", "-Wno-builtin-declaration-mismatch"]
    args += ["-D" + v for v in ("GEVR=1", "PLATFORM_64BIT=1", "_LANGUAGE_C=1", "VERSION=2", "VERSION_US=1", "LANG_US=1")]
    args += ["-I" + str(ROOT / path) for path in (".", "port/include", "include", "src", "src/game", "port/src", "port/src/net")]
    result = subprocess.run(args + [str(source), str(ROOT / "port/src/net/netbuf.c"),
                                   str(ROOT / "port/src/net/net_match.c"), "-o", str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    subprocess.run([str(exe)], check=True)
