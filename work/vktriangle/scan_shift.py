#!/usr/bin/env python3
"""尝试不同字符偏移/字节序解码，按 PM4 包头合理度打分。"""
import re

lines = open('devcd.txt', encoding='utf-8', errors='replace').read().splitlines()
raw = None
for i, ln in enumerate(lines):
    if re.search(r'iova:\s*0x0*20d3000\b', ln):
        print("iova line:", ln.strip())
        for j in range(i + 1, i + 6):
            print("  next:", lines[j][:50])
            if lines[j].strip():
                raw = lines[j].strip()
                break
        break
print("raw len:", len(raw) if raw else None)
if raw:
    def score(dws):
        hits = 0
        for w in dws:
            t = (w >> 30) & 3
            if t == 3:
                op = (w >> 8) & 0xFF
                if op in VALID or op in T3:
                    hits += 1
            elif t == 1:
                hits += 0  # T4 单独判
            if (w & 0xC0000000) == 0x40000000:
                hits += 1
            elif (w & 0xFF000000) == 0x70000000:
                hits += 1
        return hits

    def decode_shift(s, k):
        out = []
        i = k
        while i < len(s):
            if s[i] == 'z':
                out.append(0); i += 1; continue
            if not ('!' <= s[i] <= 'u'):
                break
            accum, j = 0, 0
            while j < 5 and i < len(s) and '!' <= s[i] <= 'u':
                accum = accum * 85 + (ord(s[i]) - 33); i += 1; j += 1
            out.append(accum & 0xffffffff)
        return out

    for k in range(6):
        dws = decode_shift(raw, k)
        print(f"shift={k} score={score(dws)} first=[{' '.join(f'{w:08x}' for w in dws[:6])}]")

    dws = decode_shift(raw, 0)
    bs = [int.from_bytes(w.to_bytes(4, 'little'), 'big') for w in dws]
    print(f"byteswap score={score(bs)} first=[{' '.join(f'{w:08x}' for w in bs[:6])}]")
