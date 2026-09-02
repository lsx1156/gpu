#!/usr/bin/env python3
"""Parse KERNEL-level msm RD dump (from /sys/kernel/debug/dri/../rd).

Kernel msm_rd format: [type u32][size u32 = WORD count][payload = size*4 bytes].
Record types (kernel): 3=RD_GPUADDR, 6=RD_CS, 12=RD_BUF, 13=RD_GPU_ID.
"""
import struct, sys, re

def parse_rd(path):
    data = open(path, 'rb').read()
    recs = []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from('<II', data, off)
        off += 8
        nbytes = sz * 4
        payload = data[off:off+nbytes]
        off += nbytes
        recs.append((typ, sz, payload))
    return recs

def main(path):
    recs = parse_rd(path)
    from collections import Counter
    cnt = Counter(t for t, s, p in recs)
    print('=== KRD:', path, 'records:', len(recs), '===')

    # first 5 records raw
    for i, (t, s, p) in enumerate(recs[:5]):
        print('rec %d: type=%d size_words=%d firstbytes=%s' % (i, t, s, p[:16].hex()))

    # classify
    for t in sorted(cnt):
        print('  type %2d : %d records' % (t, cnt[t]))
    total_cs = sum(s for t, s, p in recs if t == 6)
    total_buf = sum(s for t, s, p in recs if t == 12)
    print('  total CS words: %d  total BUF words: %d' % (total_cs, total_buf))

if __name__ == '__main__':
    main(sys.argv[1])
