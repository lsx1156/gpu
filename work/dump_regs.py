#!/usr/bin/env python3
"""Dump varying-relevant registers per submit (VFD fetch/decode/dest + VPC + SP)."""
import struct, sys, re
from parse_krd2 import parse_records, walk

REGS = [
    (0xe400, 'VFD_CTRL_0'), (0xe401, 'VFD_CTRL_1'), (0xe405, 'VFD_CTRL_5'),
    (0xe40a, 'FETCH0.BASE_LO'), (0xe40c, 'FETCH0.SIZE'), (0xe40d, 'FETCH0.STRIDE'),
    (0xe40e, 'FETCH1.BASE_LO'), (0xe410, 'FETCH1.SIZE'), (0xe411, 'FETCH1.STRIDE'),
    (0xe48a, 'DECODE0'), (0xe48b, 'DEC0.STEP'), (0xe48c, 'DECODE1'), (0xe48d, 'DEC1.STEP'),
    (0xe4ca, 'DEST_CNTL0'), (0xe4cb, 'DEST_CNTL1'),
    (0xe280, 'VPC_CNTL_0'), (0xe294, 'VPC_VAR0'), (0xe29d, 'VPC_PACK'),
    (0xe593, 'SP_VS_OUT0'), (0xe5a3, 'SP_VS_VPC_DST0'),
    (0xe5c0, 'SP_FS_CTRL0'), (0xe7d8, 'HLSQ_FS_INSTR'), (0xe7d7, 'HLSQ_FS_CONST'),
]


def main(path):
    recs = parse_records(path)
    bos, submits, cur = [], [], None
    for typ, payload in recs:
        if typ == 2:
            txt = payload.rstrip(b'\x00').decode('utf-8', 'replace')
            m = re.match(r'([^/]+)/(\d+): fence=(\d+)', txt)
            name = m.group(1) + '/' + m.group(2) if m else txt
            cur = {'comm': name, 'pid': '-', 'streams': [], 'n': 0}
            submits.append(cur)
            continue
        if typ == 3:
            iova, size, hi = struct.unpack('<III', payload[:12])
            bos.append([iova, size, None])
        elif typ == 12:
            for b in reversed(bos):
                if b[2] is None:
                    b[2] = payload
                    break
        elif typ == 6:
            iova, szd, hi = struct.unpack('<III', payload[:12])
            if cur is not None:
                cur['streams'].append((iova, szd))

    for idx, s in enumerate(submits):
        out = {}
        for iova, szd in s['streams']:
            lo = []
            walk(bos, iova, szd, lo)
            for reg, v in lo:
                out[reg] = v
        if 0xe400 not in out or 0xe48a not in out:
            continue
        print('### submit %d  %s/%s' % (idx, s['comm'], s['pid']))
        for reg, name in REGS:
            if reg in out:
                print('    %-14s 0x%08x' % (name, out[reg]))
        print()


if __name__ == '__main__':
    main(sys.argv[1])
