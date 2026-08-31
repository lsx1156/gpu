#!/usr/bin/env python3
"""检查 devcoredump 命令流中的 TYPE4 寄存器地址是否属于 a5xx。
用法: python check_regs.py devcd_run2.txt
"""
import re, sys
import parse_devcd

# 收集 a5xx.xml.h 全部 REG_A5XX_* 地址
a5 = set()
hdr = open(r'D:\project\gpu\work\mesa-build\src\freedreno\registers\adreno\a5xx.xml.h',
           encoding='utf-8', errors='replace').read()
for m in re.finditer(r'#define REG_A5XX_\w+\s+0x([0-9a-fA-F]+)', hdr):
    a5.add(int(m.group(1), 16))

a6 = set()
hdr6 = open(r'D:\project\gpu\work\mesa-build\src\freedreno\registers\adreno\a6xx.xml.h',
            encoding='utf-8', errors='replace').read()
a6name = {}
for m in re.finditer(r'#define (REG_A6XX_\w+)\s+0x([0-9a-fA-F]+)', hdr6):
    a6.add(int(m.group(2), 16))
    a6name.setdefault(int(m.group(2), 16), m.group(1))

path = sys.argv[1] if len(sys.argv) > 1 else 'devcd_run2.txt'
bos = parse_devcd.parse_devcd(path)

for iova, dws in sorted(bos.items()):
    if not dws:
        continue
    bad = {}
    i = 0
    while i < len(dws):
        w = dws[i]
        if (w >> 30) == 0b01:  # TYPE4
            cnt = w & 0x7F
            reg = (w >> 8) & 0x3FFFF
            if reg not in a5:
                bad[reg] = cnt
            i += 1 + cnt
        elif (w >> 28) == 0x7:  # TYPE7
            i += 1 + (w & 0x7FFF)
        elif (w >> 30) == 0b11:  # TYPE3
            i += 1 + (((w >> 16) & 0x7FFF) + 1)
        else:
            i += 1
    if bad:
        print(f"BO {iova:#x}: 非a5xx寄存器 {len(bad)} 个:")
        for reg, cnt in sorted(bad.items()):
            a6n = a6name.get(reg, '')
            tag = f"  <== {a6n}" if reg in a6 else ""
            print(f"  reg={reg:#06x} cnt={cnt}{tag}")
    else:
        print(f"BO {iova:#x}: 全部寄存器均在 a5xx 表内")
