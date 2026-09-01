#!/usr/bin/env python3
"""把 RD dump .dis 文件中出现的寄存器映射为 a5xx 寄存器名，并统计缺失。"""
import re, os, xml.etree.ElementTree as ET

here = os.path.dirname(os.path.abspath(__file__))
xmlp = os.path.join(here, "mesa-src", "mesa-24.0.5", "src", "freedreno", "registers", "adreno", "a5xx.xml")
import sys
dis = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "vktriangle", "run15_00001.dis")

tree = ET.parse(xmlp)
root = tree.getroot()
for el in root.iter():
    el.tag = el.tag.split("}")[-1]
names = {}
for reg in root.iter("reg32"):
    off = reg.get("offset"); nm = reg.get("name")
    if off and nm:
        names.setdefault(int(off, 16), []).append(nm)
# arrays: reg base names（用 xml 声明的 length，不再瞎猜）
arrays = []
for arr in root.iter("array"):
    off = arr.get("offset"); stride = arr.get("stride"); nm = arr.get("name")
    ln = arr.get("length")
    if off and nm and ln:
        arrays.append((int(off, 16), int(stride), int(ln), nm))
arrays.sort(key=lambda t: -t[0])

def regname(v):
    for base, stride, ln, nm in arrays:
        if base <= v < base + stride * ln:
            idx = (v - base) // stride
            sub = (v - base) % stride
            return f"{nm}[{idx}]{'+%d' % sub if sub else ''}"
    if v in names:
        return names[v][0]
    for base in sorted(names, reverse=True):
        if base < v:
            return f"{names[base][0]}+0x{v-base:x}"
    return "???"

seen = {}
for line in open(dis, encoding="utf-8"):
    m = re.search(r"pkt4 reg=0x0([0-9a-f]{4})", line)
    if m:
        v = int(m.group(1), 16)
        cnt = int(re.search(r"cnt=(\d+)", line).group(1))
        for k in range(cnt):
            seen.setdefault(v + k, 0)
            seen[v + k] += 1

print("total distinct regs:", len(seen))
for v in sorted(seen):
    print(f"  0x{v:05x} x{seen[v]:<3} {regname(v)}")
