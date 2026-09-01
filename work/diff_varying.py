#!/usr/bin/env python3
"""diff_varying.py - 对比 M1.4 常量绿(run16, 无varying) 与 多彩(multicolor, 有varying)
的 VPC/SP/VFD 关键寄存器。两种均为 A506 tu5xx sysmem DRAW。"""
import struct, sys, os
import xml.etree.ElementTree as ET

base = r"D:\project\gpu\work"
xmlp = os.path.join(base, "mesa-src", "mesa-24.0.5", "src", "freedreno",
                    "registers", "adreno", "a5xx.xml")
tree = ET.parse(xmlp)
root = tree.getroot()
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
        if b0 <= v < b0 + stride * ln:
            idx = (v - b0) // stride
            sub = (v - b0) % stride
            return f"{nm}[{idx}]{'+%d' % sub if sub else ''}"
    if v in names:
        return names[v][0]
    for b0 in sorted(names, reverse=True):
        if b0 < v:
            return f"{names[b0][0]}+0x{v-b0:x}"
    return "???"

def parse_rd(path):
    data = open(path, "rb").read()
    bos, cs = [], []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off)
        off += 8
        payload = data[off:off+sz]
        off += sz
        if typ == 3:
            iova, size = struct.unpack("<II", payload[:8])
            bos.append([iova, size, None])
        elif typ == 12:
            for b in reversed(bos):
                if b[2] is None:
                    b[2] = payload; break
        elif typ == 6:
            cs.append(struct.unpack("<II", payload[:8]))
    return bos, cs

def find_bo(bos, iova):
    return next((b for b in bos if b[2] and b[0] <= iova < b[0]+b[1]), None)

def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b: return None
    o = iova - b[0]
    avail = (len(b[2]) - o) // 4
    n = min(n, avail)
    return list(struct.unpack_from(f"<{n}I", b[2], o))

def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if ws is None: return
    i = 0
    while i < len(ws):
        w = ws[i]
        t = (w >> 28) & 0xF
        if t == 4:
            reg = (w >> 8) & 0x3FFFF
            c = w & 0x7F
            body = ws[i+1:i+1+c]
            for k, v in enumerate(body):
                out.append(("w", reg+k, v))
            i += 1 + c
        elif t == 7:
            op = (w >> 16) & 0x7F
            c = w & 0x7FFF
            body = ws[i+1:i+1+c]
            if op == 0x3f and c >= 3 and depth < 3:
                tgt = body[0] | (body[1] << 32)
                sz = body[2] & 0xFFFF
                walk(bos, tgt, sz, out, depth+1)
            i += 1 + c
        else:
            i += 1

def dump_stream(path):
    bos, cs = parse_rd(path)
    best = None
    for iova, sz in cs:
        out = []
        walk(bos, iova, sz, out)
        if any(e[0] == "w" for e in out):
            n = sum(1 for e in out if e[0] == "w")
            if best is None or n > best[0]:
                best = (n, out)
    if best is None: return []
    return best[1]

# VPC/SP/VFD/HLSQ 兴趣寄存器前缀
KEYS = ["VPC", "SP_", "VFD", "HLSQ", "GRAS", "RB_", "PC_PRIM", "SP_PRIMITIVE", "PC_CLIP", "GRAS_VS_CL"]

def filter_regs(stream):
    last = {}
    for idx, e in enumerate(stream):
        if e[0] == "w":
            nm = regname(e[1])
            if any(nm.startswith(k) or ("VPC" in nm or "SP_" in nm or "VFD" in nm or "HLSQ" in nm) for k in KEYS):
                last[e[1]] = (nm, e[2])
    return last

def main():
    a = dump_stream(os.path.join(base, "vktriangle", "run16_00001.rd"))
    b = dump_stream(os.path.join(base, "vktriangle", "fresh_varying_00001.rd"))
    ra = filter_regs(a)
    rb = filter_regs(b)
    print("=== 常量绿写、multicolor没写 (有varying后变0/未发) ===")
    for r in sorted(set(ra)-set(rb)):
        print(f"  0x{r:05x} {ra[r][0]:<46} = 0x{ra[r][1]:08x}")
    print("\n=== multicolor写、常量绿没写 ===")
    for r in sorted(set(rb)-set(ra)):
        print(f"  0x{r:05x} {rb[r][0]:<46} = 0x{rb[r][1]:08x}")
    print("\n=== 两边都写但值不同 ===")
    for r in sorted(set(ra)&set(rb)):
        if ra[r][1] != rb[r][1]:
            na = ra[r][0]; nb = rb[r][0]
            print(f"  0x{r:05x} {na:<46} green=0x{ra[r][1]:08x}  multi=0x{rb[r][1]:08x}")

main()