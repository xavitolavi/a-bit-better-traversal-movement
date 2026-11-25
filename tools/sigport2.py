"""Find TeleportReason (via its callers) and GUObjectArray (via any reference) in build B.
For each reference / call site in A, a pattern of the surrounding instructions (operands pointing into the image
wildcarded) is searched in B; a unique hit gives the site in B, and the target is read from B's bytes there."""
import struct, sys
from sigmake import PE, disasm, wild, find_all, fmt

A, B = sys.argv[1], sys.argv[2]
pa, pb = PE(A), PE(B)


def sites_A(target, kind):
    _, tva, tvs, traw, trs = pa.text; text = pa.d[traw:traw + trs]; out = []
    for o in range(1, len(text) - 4):
        disp = struct.unpack_from('<i', text, o)[0]
        if tva + o + 4 + disp != target: continue
        if kind == 'call' and text[o - 1] == 0xE8: out.append(tva + o - 1)
        if kind == 'data' and o >= 3 and text[o - 3] in (0x48, 0x4C) and text[o - 2] in (0x8B, 0x8D) and (text[o - 1] & 0xC7) == 0x05:
            out.append(tva + o - 3)
    return out


def port_site(site):
    ins = disasm(A, site - 40, 90)
    k = next((i for i, x in enumerate(ins) if x[0] == site), None)
    if k is None: return None
    for before, after in ((4, 4), (3, 3), (6, 6), (2, 2)):
        sel = ins[max(0, k - before):k + after + 1]
        pat, mask, off = [], [], 0
        for a, b, t in sel:
            if a == site: off = len(pat)
            pat += list(b); mask += wild(b, t)
        if sum(mask) < 16: continue
        _, tva, tvs, traw, trs = pb.text
        hits = find_all(pb.d, pat, mask, traw, traw + trs)
        if len(hits) == 1: return pb.rva_of(hits[0]) + off
    return None


for name, target, kind in (('TeleportReason', 0x1940E14, 'call'), ('GUObjectArray', 0x871EB28, 'data')):
    found = {}
    for s in sites_A(target, kind)[:60]:
        sb = port_site(s)
        if sb is None: continue
        b = pb.bytes_at(sb, 8)
        if kind == 'call': t = sb + 5 + struct.unpack_from('<i', b, 1)[0]
        else: t = sb + 7 + struct.unpack_from('<i', b, 3)[0]
        found[t] = found.get(t, 0) + 1
    print(name, 'A %08X' % target, '-> B candidates', {('%08X' % k): v for k, v in found.items()})
