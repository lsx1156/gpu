#!/usr/bin/env python3
"""完整(不过滤)对比 fd5 vs tu5xx 的写寄存器集合, 找出 fd5 写但 tu 从不写的寄存器."""
import struct, os
import xml.etree.ElementTree as ET
here = os.path.dirname(os.path.abspath(__file__))
xmlp = os.path.join(here, "mesa-src", "mesa-24.0.5", "src", "freedreno",
                    "registers", "adreno", "a5xx.xml")
tree = ET.parse(xmlp); root = tree.getroot()
for el in root.iter():
    el.tag = el.tag.split("}")[-1]
names = {}
for reg in root.iter("reg32"):
    off, nm = reg.get("offset"), reg.get("name")
    if off and nm:
        names.setdefault(int(off, 16), []).append(nm)
arrays = []
for arr in root.iter("array"):
    off, stride, nm, ln = arr.get("offset"), arr.get("stride"), arr.get("name"), arr.get("length")
    if off and nm and ln:
        arrays.append((int(off, 16), int(stride, 16), int(ln, 16), nm))
arrays.sort(key=lambda t: -t[0])
def regname(v):
    for b0, stride, ln, nm in arrays:
        if b0 <= v < b0 + stride*ln:
            idx = (v-b0)//stride; sub = (v-b0)%stride
            return f"{nm}[{idx}]{'+%d'%sub if sub else ''}"
    if v in names:
        return names[v][0]
    for b0 in sorted(names, reverse=True):
        if b0 < v:
            return f"{names[b0][0]}+0x{v-b0:x}"
    return "???"

def parse_rd(path):
    data = open(path, "rb").read(); bos, cs = [], []; off = 0
    while off+8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off); off += 8
        payload = data[off:off+sz]; off += sz
        if typ == 3:
            iova, size = struct.unpack("<II", payload[:8]); bos.append([iova, size, None])
        elif typ == 12:
            for b in reversed(bos):
                if b[2] is None: b[2] = payload; break
        elif typ == 6:
            cs.append(struct.unpack("<II", payload[:8]))
    return bos, cs
def find_bo(bos, iova):
    return next((b for b in bos if b[2] and b[0] <= iova < b[0]+b[1]), None)
def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b: return None
    o = iova-b[0]; avail = (len(b[2])-o)//4; n = min(n, avail)
    return list(struct.unpack_from(f"<{n}I", b[2], o))
def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if ws is None: return
    i = 0
    while i < len(ws):
        w = ws[i]; t = (w >> 28) & 0xF
        if t == 4:
            reg = (w >> 8) & 0x3FFFF; c = w & 0x7F
            for k, v in enumerate(ws[i+1:i+1+c]):
                out.append(("w", reg+k, v))
            i += 1 + c
        elif t == 7:
            op = (w >> 16) & 0x7F; c = w & 0x7FFF
            body = ws[i+1:i+1+c]
            if op == 0x3f and c >= 3 and depth < 3:
                tgt = body[0] | (body[1] << 32); sz = body[2] & 0xFFFF
                walk(bos, tgt, sz, out, depth+1)
            i += 1 + c
        else:
            i += 1
def dump_stream(path):
    bos, cs = parse_rd(path); best = None
    for iova, sz in cs:
        out = []; walk(bos, iova, sz, out)
        if any(e[0]=="w" for e in out):
            n = sum(1 for e in out if e[0]=="w")
            if best is None or n > best[0]: best = (n, out)
    return best[1] if best else []

fd = dump_stream(os.path.join(here, "fd5_kernel.rd"))
tu = dump_stream(os.path.join(here, "vktriangle", "fresh_varying_00001.rd"))
rf = {}; rt = {}
for e in fd:
    if e[0]=="w": rf[e[1]] = e[2]
for e in tu:
    if e[0]=="w": rt[e[1]] = e[2]
log = open(os.path.join(here, "cmp_full.txt"), "w", encoding="utf-8")
def p(*a): print(*a, file=log)
p("=== fd5 写、tu5xx(fresh_varying) 从不写 ===")
for r in sorted(set(rf)-set(rt)):
    p(f"  0x{r:05x} {regname(r):<46} = 0x{rf[r]:08x}")
p("\n总 fd5寄存器数=%d, tu寄存器数=%d" % (len(rf), len(rt)))
log.close()
print("done -> work/cmp_full.txt")