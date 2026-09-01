#!/usr/bin/env python3
"""扫描 fresh_varying RD 中 H1 候选寄存器实际取值"""
import struct, sys

REGS = {
    0x0e280: "VPC_CNTL_0",
    0x0e294: "VPC_VAR[0]",
    0x0e29d: "VPC_PACK",
    0x0e585: "SP_FS_CONFIG",
    0x0e590: "SP_VS_CTRL_REG0",
    0x0e593: "SP_VS_OUT[0]",
    0x0e5a3: "SP_VS_VPC_DST[0]",
    0x0e5c0: "SP_FS_CTRL_REG0",
    0x0e78c: "HLSQ_FS_CONFIG",
    0x0e792: "HLSQ_FS_CNTL",
    0x0e784: "HLSQ_CONTROL_0_REG",
    0x0e7d7: "HLSQ_FS_CONSTLEN",
    0x0e7d8: "HLSQ_FS_INSTRLEN",
    0x0e5d4: "SP_FS_MRT[0]",
}

def parse_rd(path):
    data = open(path, "rb").read()
    bos, cs = [], []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off)
        off += 8
        payload = data[off:off+sz]
        off += sz
        if typ == 3:  # RD_GPUADDR
            iova, size = struct.unpack("<II", payload[:8])
            bos.append([iova, size, None])
        elif typ == 12:  # RD_BUFFER_CONTENTS
            for b in reversed(bos):
                if b[2] is None:
                    b[2] = payload; break
        elif typ == 6:  # RD_CMDSTREAM_ADDR
            cs.append(struct.unpack("<II", payload[:8]))
    return bos, cs

def find_bo(bos, iova):
    return next((b for b in bos if b[2] and b[0] <= iova < b[0]+b[1]), None)

def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b: return None
    o = iova - b[0]
    avail = (len(b[2]) - o)//4
    return list(struct.unpack_from(f"<{min(n,avail)}I", b[2], o))

def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if ws is None: return
    i = 0
    while i < len(ws):
        w = ws[i]; t = (w>>28)&0xF
        if t == 4:
            reg = (w>>8)&0x3FFFF; c = w&0x7F
            body = ws[i+1:i+1+c]
            for k,v in enumerate(body): out.append(("w", reg+k, v))
            i += 1+c
        elif t == 7:
            op=(w>>16)&0x7F; c=w&0x7FFF; body=ws[i+1:i+1+c]
            if op==0x3f and c>=3 and depth<4:
                tgt=body[0]|(body[1]<<32); sz=body[2]&0xFFFF
                walk(bos, tgt, sz, out, depth+1)
            i += 1+c
        else:
            i += 1

def dump(path):
    bos, cs = parse_rd(path)
    seen = {}
    for iova, sz in cs:
        out = []
        walk(bos, iova, sz, out)
        for _, reg, v in out:
            if reg in REGS:
                seen[reg] = v
    print("== ", path)
    for r in sorted(REGS):
        if r in seen:
            print(f"  0x{r:05x} {REGS[r]:<18} = 0x{seen[r]:08x}")
        else:
            print(f"  0x{r:05x} {REGS[r]:<18} = (未写)")

if len(sys.argv) > 1:
    dump(sys.argv[1])
else:
    dump(r"d:\project\gpu\work\vktriangle\fresh_varying_00001.rd")
    dump(r"d:\project\gpu\work\vktriangle\multicolor_00001.rd")