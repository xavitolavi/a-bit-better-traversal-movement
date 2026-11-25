"""Byte patterns for every game address the traversal DLL uses, so it can find them by search instead of fixed RVAs
(the step towards supporting game version 1.0.0.4 next to 1.0.0.5).

usage:
  python sigmake.py make  <exe>            build patterns from this exe (the 1.0.0.5 one), check each is unique,
                                           write sigs.json and ../dll/sigs_data.inc
  python sigmake.py check <exe>            look the patterns up in another exe (e.g. 1.0.0.4) and print each RVA found

Code addresses: the pattern starts at the address. Bytes of operands that point elsewhere (call/jmp/jcc rel32,
RIP-relative displacements) are wildcarded, using dumpbin's disassembly to find instruction boundaries.
Data addresses (GUObjectArray, FNamePool blocks): a RIP-relative instruction that references them is found by brute
force in .text; its pattern is stored with the offset of the displacement, and the DLL resolves the target at run time.
Read-only on the exe.
"""
import json, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DUMPBIN_BAT = os.path.join(HERE, '_dumpbin_range.bat')
IMAGE_BASE = 0x140000000

# name -> (kind, rva in 1.0.0.5)
TARGETS = {
    'ProcessEvent':      ('code', 0x19981F0),
    'ParkourTry':        ('code', 0x345FCC4),
    'ParkourUpdate':     ('code', 0x1301444),
    'ActRequest':        ('code', 0x1511E94),
    'LedgeDetect':       ('code', 0x13036A4),
    'TeleportReason':    ('code', 0x1940E14),
    'AutoTrav':          ('code', 0x1301F93),
    'AutoTravNo':        ('code', 0x1301FD8),
    'AutoTravYes':       ('code', 0x13028AD),
    'PromptReach':       ('code', 0x12FF4F4),
    'SigSite':           ('code', 0x10BE2DC),
    'GUObjectArray':     ('data', 0x871EB28),
    'NameBlocks':        ('data', 0x90D3C10),
}


class PE:
    def __init__(self, path):
        self.path = path; self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', self.d, 0x3C)[0]
        nsec = struct.unpack_from('<H', self.d, pe + 6)[0]; opt = struct.unpack_from('<H', self.d, pe + 20)[0]
        self.secs = []
        for i in range(nsec):
            o = pe + 24 + opt + 40 * i
            name = self.d[o:o + 8].rstrip(b'\0').decode()
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.d, o + 8)
            self.secs.append((name, va, vsize, raw, rsize))
        self.text = next(s for s in self.secs if s[0] == '.text')

    def off(self, rva):
        for _, va, vs, raw, rs in self.secs:
            if va <= rva < va + max(vs, rs): return raw + rva - va
        raise ValueError(hex(rva))

    def rva_of(self, off):
        for _, va, vs, raw, rs in self.secs:
            if raw <= off < raw + rs: return va + off - raw
        raise ValueError(hex(off))

    def bytes_at(self, rva, n): o = self.off(rva); return self.d[o:o + n]


def disasm(exe, rva, n):
    """[(rva, bytes, text)] for the instructions in [rva, rva+n) via dumpbin."""
    if not os.path.exists(DUMPBIN_BAT):
        open(DUMPBIN_BAT, 'w').write('@echo off\r\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>nul\r\n'
                                     'dumpbin /nologo /disasm /range:%1,%2 %3\r\n')
    a0, a1 = IMAGE_BASE + rva, IMAGE_BASE + rva + n
    out = subprocess.run(['cmd', '/c', DUMPBIN_BAT, hex(a0), hex(a1), exe], capture_output=True, text=True).stdout
    ins = []
    for line in out.splitlines():
        m = re.match(r'\s+([0-9A-F]{16}): ((?:[0-9A-F]{2} )+)\s*(.*)$', line)
        if m:
            ins.append([int(m.group(1), 16) - IMAGE_BASE, bytes.fromhex(m.group(2).replace(' ', '')), m.group(3).strip()])
        else:
            m2 = re.match(r'\s+([0-9A-F]{2}(?: [0-9A-F]{2})*)\s*$', line)          # continuation of a long instruction's bytes
            if m2 and ins: ins[-1][1] += bytes.fromhex(m2.group(1).replace(' ', ''))
    return ins


def wild(ins_bytes, text):
    """mask (True = keep) for one instruction: operands that hold a relative address become wildcards."""
    n = len(ins_bytes); keep = [True] * n
    if re.search(r'000000014[0-9A-F]{7}', text):         # points into the image: rel32 or [rip+disp32]
        op = ins_bytes[0]
        if op in (0xE8, 0xE9) and n == 5: keep[1:] = [False] * 4
        elif op == 0x0F and n == 6 and 0x80 <= ins_bytes[1] <= 0x8F: keep[2:] = [False] * 4
        else:                                              # RIP-relative: keep the opcode bytes, wildcard the rest
            k = 3 if ins_bytes[0] in (0x40, 0x41, 0x44, 0x45, 0x48, 0x49, 0x4C, 0x4D, 0xC5, 0xC4) else 2
            if ins_bytes[0] in (0xC5,): k = 3
            keep[k:] = [False] * (n - k)
    return keep


def find_all(data, pat, mask, lo, hi, first_only=False):
    """offsets in data[lo:hi] where pat matches (mask False = any byte)."""
    anchor = max(range(len(pat)), key=lambda i: (mask[i], -i)) if any(mask) else 0
    # use the longest run of fixed bytes as the search key
    best, cur, start = (0, 0), 0, 0
    for i, m in enumerate(mask + [False]):
        if m: cur += 1
        else:
            if cur > best[1] - best[0]: best = (i - cur, i)
            cur = 0
    k0, k1 = best; key = bytes(pat[k0:k1]); res = []
    i = data.find(key, lo + k0, hi)
    while i != -1:
        s = i - k0
        if s >= lo and s + len(pat) <= hi and all(not mask[j] or data[s + j] == pat[j] for j in range(len(pat))):
            res.append(s)
            if first_only or len(res) > 1: break
        i = data.find(key, i + 1, hi)
    return res


def make(exe):
    pe = PE(exe); _, tva, tvs, traw, trs = pe.text; lo, hi = traw, traw + trs
    sigs = {}
    for name, (kind, rva) in TARGETS.items():
        if kind == 'code':
            ins = disasm(exe, rva, 96)
            if not ins or ins[0][0] != rva: raise SystemExit('%s: no instruction starts at %x' % (name, rva))
            pat, mask = [], []
            for a, b, txt in ins:
                pat += list(b); mask += wild(b, txt)
                if len(pat) >= 16:
                    hits = find_all(pe.d, pat, mask, lo, hi)
                    if len(hits) == 1 and sum(mask) >= 12: break
            hits = find_all(pe.d, pat, mask, lo, hi)
            if len(hits) != 1: raise SystemExit('%s: pattern not unique (%d hits)' % (name, len(hits)))
            sigs[name] = {'kind': 'code', 'rva': rva, 'pattern': fmt(pat, mask), 'disp': -1}
        else:
            # brute-force RIP-relative references: int32 at off, instruction ends at off+4 (no immediate)
            refs = []
            text = pe.d[lo:hi]
            for o in range(len(text) - 4):
                disp = struct.unpack_from('<i', text, o)[0]
                tgt = tva + o + 4 + disp
                if rva - 0x40 <= tgt <= rva and o >= 3:
                    p0, p1, p2 = text[o - 3], text[o - 2], text[o - 1]
                    if p0 in (0x48, 0x4C) and p1 in (0x8B, 0x8D) and (p2 & 0xC7) == 0x05: refs.append((rva - tgt, o - 3))
            refs.sort()                                   # direct references (delta 0) first
            if not refs: raise SystemExit('%s: no simple RIP-relative reference found' % name)
            done = False
            for delta, r in refs[:200]:
                site = tva + r
                ins = disasm(exe, site - 24, 64)
                start = next((i for i, x in enumerate(ins) if x[0] == site), None)
                if start is None: continue
                # pattern = a few instructions before + the reference (disp wildcarded) + a few after
                for before in (3, 2, 1, 0):
                    s0 = max(0, start - before); sel = ins[s0:start + 4]
                    pat, mask = [], []; dpos = None
                    for a, b, txt in sel:
                        if a == site: dpos = len(pat) + 3; m = [True, True, True, False, False, False, False]
                        else: m = wild(b, txt)
                        pat += list(b); mask += m
                    hits = find_all(pe.d, pat, mask, lo, hi)
                    if len(hits) == 1 and sum(mask) >= 14:
                        sigs[name] = {'kind': 'data', 'rva': rva, 'pattern': fmt(pat, mask), 'disp': dpos, 'delta': delta}
                        done = True; break
                if done: break
            if not done: raise SystemExit('%s: no unique reference pattern' % name)
        print('%-15s %-4s %08X  %s' % (name, kind, rva, sigs[name]['pattern']))
    json.dump(sigs, open(os.path.join(HERE, 'sigs.json'), 'w'), indent=1)
    write_inc(sigs)


def fmt(pat, mask): return ' '.join('%02X' % b if m else '??' for b, m in zip(pat, mask))


def parse(s):
    toks = s.split(); return [0 if t == '??' else int(t, 16) for t in toks], [t != '??' for t in toks]


def resolve(pe, sig):
    _, tva, tvs, traw, trs = pe.text
    pat, mask = parse(sig['pattern'])
    hits = find_all(pe.d, pat, mask, traw, traw + trs)
    if len(hits) != 1: return None, len(hits)
    site = pe.rva_of(hits[0])
    if sig['kind'] == 'code': return site, 1
    disp = struct.unpack_from('<i', pe.d, hits[0] + sig['disp'])[0]
    return site + sig['disp'] + 4 + disp + sig.get('delta', 0), 1


def check(exe):
    pe = PE(exe); sigs = json.load(open(os.path.join(HERE, 'sigs.json')))
    ok = True
    for name, sig in sigs.items():
        r, n = resolve(pe, sig)
        same = (r == sig['rva'])
        print('%-15s %s' % (name, ('%08X%s' % (r, '  (same as 1.0.0.5)' if same else '')) if r is not None else 'NOT FOUND (%d hits)' % n))
        ok &= r is not None
    print('all found' if ok else 'SOME MISSING')


def write_inc(sigs):
    p = os.path.join(os.path.dirname(HERE), 'dll', 'sigs_data.inc')
    with open(p, 'w') as f:
        f.write('// generated by tools/sigmake.py - do not edit. Patterns for the game addresses the DLL uses\n')
        f.write('// ("??" = any byte). kind 0 = code address at the match, 1 = data: RIP-relative displacement at +disp.\n')
        f.write('struct GameSig { const char* name; const char* pattern; int kind; int disp; int delta; uint32_t rva1005; };\n')
        f.write('static const GameSig GAME_SIGS[] = {\n')
        for name, s in sigs.items():
            f.write('    { "%s", "%s", %d, %d, %d, 0x%X },\n' % (name, s['pattern'], 0 if s['kind'] == 'code' else 1, s['disp'], s.get('delta', 0), s['rva']))
        f.write('};\n')
    print('wrote', p)


if __name__ == '__main__':
    {'make': make, 'check': check}[sys.argv[1]](sys.argv[2])
