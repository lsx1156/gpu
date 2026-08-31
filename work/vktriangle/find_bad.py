#!/usr/bin/env python3
"""在 devcd BO 中搜索特定 dword 及上下文"""
import sys, importlib.util
spec = importlib.util.spec_from_file_location("pd", r"d:\project\gpu\work\vktriangle\parse_devcd.py")
pd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pd)

bos = pd.parse_devcd(sys.argv[1])
targets = [int(x, 16) for x in sys.argv[2:]]
for t in targets:
    for k, v in bos.items():
        for i, w in enumerate(v):
            if w == t:
                lo, hi = max(0, i - 6), min(len(v), i + 7)
                print(f"0x{t:08x} @ BO 0x{k:x}[{i}]")
                for j in range(lo, hi):
                    mark = " <<<<" if j == i else ""
                    print(f"   [{j:5d}] 0x{v[j]:08x}{mark}")
                # 尝试按 PM4 解析命中点前后的包
                print("   --- walk from group start candidates ---")
                break
