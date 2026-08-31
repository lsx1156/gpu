#!/usr/bin/env python3
"""解码 IB2 中 CP_SET_DRAW_STATE 组表 + 逐组 walk 内容"""
import sys, importlib.util
spec = importlib.util.spec_from_file_location("pd", r"d:\project\gpu\work\vktriangle\parse_devcd.py")
pd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pd)

GID_NAMES = {0:"PROGRAM_CONFIG",1:"VS",2:"VS_BINNING",3:"HS",4:"DS",5:"GS",6:"GS_BINNING",
             7:"VPC",8:"FS",9:"VB",10:"CONST",11:"DESC_SETS",12:"DESC_SETS_LOAD",
             13:"VS_PARAMS",14:"FS_PARAMS",15:"INPUT_ATT_GMEM",16:"INPUT_ATT_SYSMEM",
             17:"LRZ",18:"PRIM_GMEM",19:"PRIM_SYSMEM"}

bos = pd.parse_devcd(sys.argv[1])
ib = bos[int(sys.argv[2], 16)]

def walk(dwords, label, maxpk=4000):
    print(f"==== {label}: {len(dwords)} dwords ====")
    i = 0; npk = 0
    while i < len(dwords) and npk < maxpk:
        w = dwords[i]
        if (w >> 28) == 0x7:
            cnt = w & 0x7FFF; op = (w >> 16) & 0x7F
            name = pd.T3.get(op, f"OP_{op}")
            body = dwords[i+1:i+1+cnt]
            print(f"[{i:5d}] T7 {name}({op}) cnt={cnt} body={' '.join(f'{x:08x}' for x in body[:12])}{' ...' if cnt>12 else ''}")
            i += 1 + cnt
        elif (w >> 30) == 0b01:
            cnt = w & 0x7F; reg = (w >> 8) & 0x3FFFF
            body = dwords[i+1:i+1+cnt]
            print(f"[{i:5d}] T4 reg={reg:#06x} cnt={cnt} vals=" + ' '.join(f'{x:08x}' for x in body[:12]))
            i += 1 + cnt
        elif (w >> 30) == 0b11:
            print(f"[{i:5d}] ?? pkt7-odd 0x{w:08x} <-- 失步?"); i += 1
        elif (w >> 30) == 0b10:
            print(f"[{i:5d}] T2 0x{w:08x}"); i += 1
        else:
            print(f"[{i:5d}] ?? 0x{w:08x} <-- 失步?"); i += 1
        npk += 1

# 扫描 IB 中的 CP_SET_DRAW_STATE 包
i = 0
while i < len(ib):
    w = ib[i]
    if (w >> 28) == 0x7 and ((w >> 16) & 0x7F) == 67:
        cnt = w & 0x7FFF
        body = ib[i+1:i+1+cnt]
        print(f"=== CP_SET_DRAW_STATE @ IB[{i}] cnt={cnt} ===")
        for g in range(cnt // 3):
            w0, lo, hi = body[g*3:g*3+3]
            count = w0 & 0xFFFF
            dirty = (w0 >> 16) & 1; dis = (w0 >> 17) & 1; disall = (w0 >> 18) & 1
            li = (w0 >> 19) & 1; bin_ = (w0 >> 20) & 1; gmem = (w0 >> 21) & 1; sysm = (w0 >> 22) & 1
            gid = (w0 >> 24) & 0x1F
            addr = (hi << 32) | lo
            nm = GID_NAMES.get(gid, "DYNAMIC+%d" % (gid-20))
            print(f"  grp[{g:2d}] gid={gid:2d}({nm:18s}) count={count:4d} addr=0x{addr:09x} "
                  f"LOAD_IMMED={li} DISABLE={dis} DIS_ALL={disall} flags=B{bin_}/G{gmem}/S{sysm}")
            if count and not dis and not disall:
                base = addr & ~0xFFF
                if base in bos:
                    off = (addr - base) // 4
                    bo = bos[base]
                    if off + count <= len(bo):
                        walk(bo[off:off+count], f"  gid{gid} content @ {addr:#x}")
                    else:
                        print(f"    !! group content 越界: off={off} count={count} bo_len={len(bo)}")
                else:
                    print(f"    !! group BO 0x{base:x} 不在 devcd 中")
        i += 1 + cnt
    else:
        i += 1
