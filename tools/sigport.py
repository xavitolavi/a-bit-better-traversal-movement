"""Port addresses from game build A (1.0.0.5) to build B (e.g. 1.0.0.4) by aligning instruction sequences.

A known matching anchor (an address found in both builds, e.g. by sigmake's patterns) pins a window in each build; the
windows are disassembled (dumpbin) and their mnemonic sequences aligned with difflib. Any instruction of A inside the
window maps to the aligned instruction of B; a call in A maps to the call in B, whose target is read from B's bytes.

usage: python sigport.py <exeA> <exeB>      (ports the ParkourUpdate family from the AutoTravYes anchor)
"""
import difflib, struct, sys
from sigmake import PE, disasm


def window(exe, start, n):
    ins = disasm(exe, start, n)
    return ins, [(t.split()[0] if t else '?') for _, _, t in ins]


def align(exeA, a0, nA, exeB, b0, nB):
    insA, mA = window(exeA, a0, nA); insB, mB = window(exeB, b0, nB)
    sm = difflib.SequenceMatcher(None, mA, mB, autojunk=False)
    amap = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1): amap[insA[i1 + k][0]] = (insB[j1 + k][0], insB[j1 + k][1], insB[j1 + k][2])
    return amap, sm.ratio(), insA, insB


def call_target(rva, b):
    if b[0] == 0xE8 and len(b) == 5: return rva + 5 + struct.unpack('<i', b[1:5])[0]
    return None


if __name__ == '__main__':
    A, B = sys.argv[1], sys.argv[2]
    # anchor: AutoTravYes = 0x13028AD (A) / found in B by its pattern; ParkourUpdate starts 0x1469 bytes before it in A
    PU_A, YES_A = 0x1301444, 0x13028AD
    import json, os
    from sigmake import resolve
    sigs = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sigs.json')))
    yesB, n = resolve(PE(B), sigs['AutoTravYes'])
    if yesB is None: raise SystemExit('anchor AutoTravYes not found in B')
    back = YES_A - PU_A
    amap, ratio, insA, insB = align(A, PU_A, back + 0x400, B, yesB - back - 0x200, back + 0x800)
    print('ParkourUpdate window similarity %.2f, %d instructions aligned' % (ratio, len(amap)))
    out = {}
    for name, a in (('ParkourUpdate', PU_A), ('AutoTrav', 0x1301F93), ('AutoTravNo', 0x1301FD8), ('AutoTravYes', YES_A)):
        m = amap.get(a); out[name] = m[0] if m else None
        print('%-14s A %08X -> B %s' % (name, a, '%08X' % m[0] if m else 'not aligned'))
    for name, site in (('ParkourTry', 0x1302529), ('ActRequest', 0x130257A)):
        m = amap.get(site)
        tgt = call_target(m[0], m[1]) if m else None
        out[name] = tgt
        print('%-14s call at A %08X -> B %s -> target %s' % (name, site, '%08X' % m[0] if m else '-', '%08X' % tgt if tgt else '-'))
    json.dump({k: v for k, v in out.items() if v}, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ported_B.json'), 'w'), indent=1)
