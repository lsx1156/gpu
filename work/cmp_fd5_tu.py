#!/usr/bin/env python3
"""fd5（内核 rd）vs tu5xx（TU_DEBUG=rd）draw IB2 寄存器序列对比。
输出：fd5 写了而 tu 没写的寄存器、两边都写但值不同的寄存器、顺序摘要。"""
import struct, sys, os
import xml.etree.ElementTree as ET

here = os.path.dirname(os.path.abspath(__file__))
xmlp = os.path.join(here, "mesa-src", "mesa-24.0.5", "src", "freedreno",
                    "registers", "adreno", "a5xx.xml")

# ---------- a5xx 寄存器名 ----------
tree = ET.parse(xmlp)
root = tree.getroot()
for el in root.iter():
    el.tag = el.tag.split("}")[-1]
names = {}
for reg in root.iter("reg32"):
    off = reg.get("offset"); nm = reg.get("name")
    if off and nm:
        names.setdefault(int(off, 16), []).append(nm)
arrays = []
for arr in root.iter("array"):
    off, stride, nm, ln = arr.get("offset"), arr.get("stride"), arr.get("name"), arr.get("length")
    if off and nm and ln:
        arrays.append((int(off, 16), int(stride, 16), int(ln, 16), nm))
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

# ---------- RD 解析 ----------
def parse_rd(path):
    data = open(path, "rb").read()
    bos, cs_addrs = [], []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off)
        off += 8
        payload = data[off:off + sz]
        off += sz
        if typ == 3:
            iova, size = struct.unpack("<II", payload[:8])
            bos.append([iova, size, None])
        elif typ == 12:
            for b in reversed(bos):
                if b[2] is None:
                    b[2] = payload
                    break
        elif typ == 6:
            cs_addrs.append(struct.unpack("<II", payload[:8]))
    return bos, cs_addrs

def find_bo(bos, iova):
    return next((b for b in bos if b[2] and b[0] <= iova < b[0] + b[1]), None)

def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b:
        return None
    o = iova - b[0]
    avail = (len(b[2]) - o) // 4
    n = min(n, avail)
    return list(struct.unpack_from(f"<{n}I", b[2], o))

# ---------- 顺序展开命令流（IB1 -> IB2 递归） ----------
def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if ws is None:
        return
    i = 0
    while i < len(ws):
        w = ws[i]
        t = (w >> 28) & 0xF
        if t == 4:
            reg = (w >> 8) & 0x3FFFF
            c = w & 0x7F
            body = ws[i+1:i+1+c]
            for k, v in enumerate(body):
                out.append(("w", reg + k, v))
            i += 1 + c
        elif t == 7:
            op = (w >> 16) & 0x7F
            c = w & 0x7FFF
            body = ws[i+1:i+1+c]
            nm = {0x38: "DRAW", 0x30: "LOAD_STATE4", 0x3f: "IB",
                  0x43: "SET_DRAW_STATE", 0x22: "SET_RENDER_MODE",
                  0x64: "SET_VISIBILITY_OVERRIDE"}.get(op, f"op{op:02x}")
            out.append(("p", nm, [f"0x{x:08x}" for x in body[:4]]))
            if op == 0x3f and c >= 3 and depth < 3:
                tgt = body[0] | (body[1] << 32)
                sz = body[2] & 0xFFFF
                walk(bos, tgt, sz, out, depth + 1)
            i += 1 + c
        else:
            i += 1

def dump_stream(path, label):
    bos, cs = parse_rd(path)
    # 找含 DRAW 的 IB 链：对每个 IB1 展开，找 DRAW
    best = None
    for iova, sz in cs:
        out = []
        walk(bos, iova, sz, out)
        if any(e[1] == "DRAW" for e in out if e[0] == "p"):
            # 取含 DRAW 最多的链
            n = sum(1 for e in out if e[0] == "p" and e[1] == "DRAW")
            if best is None or n > best[0]:
                best = (n, out)
    if best is None:
        print(f"{label}: 未找到 DRAW")
        return None
    return best[1]

def regs_summary(stream):
    """最后一个值优先（覆盖语义），保留首次出现位置"""
    last, first_pos = {}, {}
    for idx, e in enumerate(stream):
        if e[0] == "w":
            last[e[1]] = e[2]
            first_pos.setdefault(e[1], idx)
    return last, first_pos

fd = dump_stream(os.path.join(here, "fd5_kernel.rd"), "fd5")
tu = dump_stream(os.path.join(here, "vktriangle", "run16_00001.rd"), "tu5xx")
if fd is None or tu is None:
    sys.exit(1)

# 输出重定向到文件（避免 PowerShell 管道问题）
_log = open(os.path.join(here, "cmp_fd5_tu.txt"), "w", encoding="utf-8")
_p = print
def print(*a, **k):
    _p(*a, **k, file=_log)

fdl, _ = regs_summary(fd)
tul, _ = regs_summary(tu)

print("=== fd5 写了、tu5xx 没写的寄存器（按地址排序）===")
for r in sorted(set(fdl) - set(tul)):
    print(f"  0x{r:05x} {regname(r):<44} = 0x{fdl[r]:08x}")

print("\n=== 两边都写但值不同 ===")
for r in sorted(set(fdl) & set(tul)):
    if fdl[r] != tul[r]:
        print(f"  0x{r:05x} {regname(r):<44} fd5=0x{fdl[r]:08x} tu=0x{tul[r]:08x}")

print("\n=== tu5xx 写了、fd5 没写的寄存器 ===")
for r in sorted(set(tul) - set(fdl)):
    print(f"  0x{r:05x} {regname(r):<44} = 0x{tul[r]:08x}")

# pkt7 序列对比
def pkts(stream, names):
    return [e for e in stream if e[0] == "p" and e[1] in names]
names = {"DRAW", "LOAD_STATE4", "SET_RENDER_MODE", "SET_VISIBILITY_OVERRIDE"}
print("\n=== fd5 pkt7 序列（关键）===")
for e in pkts(fd, names):
    print(f"  {e[1]:<24} {e[2]}")
print("\n=== tu5xx pkt7 序列（关键）===")
for e in pkts(tu, names):
    print(f"  {e[1]:<24} {e[2]}")
_log.close()
_p("written: cmp_fd5_tu.txt")
