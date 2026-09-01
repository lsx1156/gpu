#!/usr/bin/env python3
"""统计 RD 文件 section 类型分布，修正 redump.h 的 type 语义。"""
import struct, sys

RD_GPUADDR, RD_CMDSTREAM, RD_CMDSTREAM_ADDR = 3, 4, 5
RD_BUFFER_CONTENTS, RD_GPU_ID, RD_CHIP_ID = 12, 13, 14

names = {3:"GPUADDR",4:"CMDSTREAM",5:"CMDSTREAM_ADDR",12:"BUFFER_CONTENTS",13:"GPU_ID",14:"CHIP_ID"}

for path in sys.argv[1:]:
    data = open(path, "rb").read()
    off = 0
    counts = {}
    cmds = []
    maxbo = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from("<II", data, off)
        off += 8
        payload = data[off:off+sz]
        off += sz
        counts[typ] = counts.get(typ, 0) + 1
        if typ == RD_CMDSTREAM_ADDR:
            iova, size = struct.unpack("<II", payload[:8])
            cmds.append((iova, size))
        elif typ == RD_GPUADDR:
            maxbo += 1
    print(f"== {path}")
    for t in sorted(counts):
        print(f"  type {t:2d} {names.get(t,'?'):<16} x{counts[t]}")
    print(f"  cmdstream_addr: {len(cmds)}  bos: {maxbo}")
    if cmds:
        print("  first 5 cmd: " + ", ".join(f"0x{io:08x}+0x{sz:x}" for io, sz in cmds[:5]))
    print()