"""Compare the instruction sequence (mnemonics only) at an address in two exes - is a pattern hit in another game
build really the same function? usage: python sigcompare.py <exeA> <rvaA hex> <exeB> <rvaB hex> [n bytes]"""
import difflib, sys
from sigmake import disasm


def mnem(exe, rva, n):
    return [t.split()[0] if t else '?' for _, _, t in disasm(exe, rva, n)]


if __name__ == '__main__':
    a, ra, b, rb = sys.argv[1], int(sys.argv[2], 16), sys.argv[3], int(sys.argv[4], 16)
    n = int(sys.argv[5]) if len(sys.argv) > 5 else 400
    ma, mb = mnem(a, ra, n), mnem(b, rb, n)
    r = difflib.SequenceMatcher(None, ma, mb).ratio()
    print('%d / %d instructions, similarity %.2f' % (len(ma), len(mb), r))
