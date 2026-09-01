#!/usr/bin/env python3
"""fd5(内核rd, GL=工作) vs tu5xx(fresh_varying=坏) 关键 varying 递送寄存器逐项对比。
聚焦 SP/VPC/HLSQ 与 FS varying 基址/递送相关。"""
import struct, os
import xml.etree.ElementTree as ET

here = os.path.dirname(os.path.abspath(__file__))
xmlp = os.path.join(here, "mesa-src", "mesa-24.0.5", "src", "freedreno",
                    "registers", "adreno", "a5xx.xml")
tree = ET.parse(xmlp); root = tree.getroot()
for el in root.iter(): el.tag = el.tag.split("}")[-1]
names = {}
for reg in root.iter("reg32"):
    off, nm = reg.get("offset"), reg.get("name")
    if off and nm: names.setdefault(int(off, 16), []).append(nm)
arrays = []
for arr in root.iter("array"):
    off, stride, nm, ln = arr.get("offset"), arr.get("stride"), arr.get("name"), arr.get("length")
    if off and nm and ln: arrays.append((int(off, 16), int(stride, 16), int(ln, 16), nm))
arrays.sort(key=lambda t: -t[0])
def regname(v):
    for b0, stride, ln, nm in arrays:
        if b0 <= v < b0 + stride * ln:
            idx = (v - b0) // stride
            return f"{nm}[{idx}]{'+%d' % ((v-b0)%stride) if (v-b0)%stride else ''}"
    if v in names: return names[v][0]
    for b0 in sorted(names, reverse=True):
        if b0 < v: return f"{names[b0][0]}+0x{v-b0:x}"
    return "???"

RD_GPUADDR, RD_BUF, RD_CS = 3, 12, 6
def parse_rd(path):
    data = open(path, "rb").read()
    bos, cs = [], []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off)
        off += 8; payload = data[off:off+sz]; off += sz
        if typ == RD_GPUADDR:
            iova, size = struct.unpack("<II", payload[:8]); bos.append([iova, size, None])
        elif typ == RD_BUF:
            for b in reversed(bos):
                if b[2] is None: b[2] = payload; break
        elif typ == RD_CS:
            cs.append(struct.unpack("<II", payload[:8]))
    return bos, cs

def find_bo(bos, iova):
    return next((b for b in bos if b[2] and b[0] <= iova < b[0]+b[1]), None)
def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b: return None
    o = iova - b[0]; avail = (len(b[2])-o)//4; n = min(n, avail)
    return list(struct.unpack_from(f"<{n}I", b[2], o))

def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if ws is None: return
    i = 0
    while i < len(ws):
        w = ws[i]; t = (w>>28)&0xF
        if t == 4:
            reg = (w>>8)&0x3FFFF; c = w&0x7F; body = ws[i+1:i+1+c]
            for k, v in enumerate(body): out.append(("w", reg+k, v))
            i += 1+c
        elif t == 7:
            op=(w>>16)&0x7F; c=w&0x7FFF; body=ws[i+1:i+1+c]
            if op==0x3f and c>=3 and depth<3:
                tgt=body[0]|(body[1]<<32); sz=body[2]&0xFFFF
                walk(bos, tgt, sz, out, depth+1)
            i += 1+c
        else: i += 1

def dump_stream(path):
    bos, cs = parse_rd(path)
    best = None
    for iova, sz in cs:
        out = []
        walk(bos, iova, sz, out)
        if any(e[0]=="w" for e in out):
            n = sum(1 for e in out if e[0]=="w")
            if best is None or n > best[0]: best = (n, out)
    if best is None: return {}
    last = {}
    for e in best[1]:
        if e[0]=="w": last[e[1]] = e[2]
    return last

fd = dump_stream(os.path.join(here, "fd5_kernel.rd"))
tu = dump_stream(os.path.join(here, "vktriangle", "fresh_cur_00001.rd"))

# 兴趣寄存器: varying 递送链
KEYS = [0x0e29d, 0x0e280, 0x0e294, 0x0e29a, 0x0e298, 0x0e285, 0x0e293,
        0x0e590, 0x0e593, 0x0e5a3, 0x0e5c0, 0x0e5c6, 0x0e583, 0x0e584, 0x0e585, 0x0e586,
        0x0e78c, 0x0e792, 0x0e784, 0x0e7d7, 0x0e7d8, 0x0e5cb, 0x0e5cc,
        0x0e2a2, 0x0e144, 0x0e587, 0x0e5a0, 0x0e5a6, 0x0e5c1, 0x0e5c2, 0x0e5d4,
        0x0e4c0, 0x0e4c1, 0x0e4ca, 0x0e4cb]

print("=== 关键寄存器对比: fd5(GL,工作) vs tu(fresh_varying,坏) ===")
for r in KEYS:
    if r in fd or r in tu:
        fv = fd.get(r); tv = tu.get(r)
        fstr = f"0x{fv:08x}" if fv is not None else "--------"
        tstr = f"0x{tv:08x}" if tv is not None else "--------"
        mark = "  " if fv==tv else "!!" if fv is not None and tv is not None else " -"
        print(f"{mark} 0x{r:05x} {regname(r):<34} fd5={fstr}  tu={tstr}")