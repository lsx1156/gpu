#!/usr/bin/env python3
"""Parse KERNEL-level msm RD (from debugfs rd node), attribute command streams
to submit processes via RD_CMD "comm/pid: fence=" records, and extract the
key varying-related registers per process.

Kernel record format: [type u32][size u32 BYTES][payload].
types: 1=RD_TEST 2=RD_CMD(ascii) 3=RD_GPUADDR{iova,size,iova_hi}
       5=RD_CMDSTREAM 6=RD_CMDSTREAM_ADDR{iova,szd,iova_hi}
       12=RD_BUFFER_CONTENTS 13=RD_GPU_ID 14=RD_CHIP_ID
"""
import struct, sys, re

def parse_records(path):
    data = open(path, 'rb').read()
    recs = []
    off = 0
    while off + 8 <= len(data):
        typ, sz = struct.unpack_from('<II', data, off)
        off += 8
        payload = data[off:off+sz]
        off += sz
        recs.append((typ, payload))
    return recs

def find_bo(bos, iova):
    return next((b for b in bos if b[2] is not None and b[0] <= iova < b[0] + b[1]), None)

def words_of(bos, iova, n):
    b = find_bo(bos, iova)
    if not b:
        return None
    o = iova - b[0]
    n = min(n, (len(b[2]) - o) // 4)
    if n <= 0:
        return []
    return list(struct.unpack_from('<%dI' % n, b[2], o))

def walk(bos, iova, n, out, depth=0):
    ws = words_of(bos, iova, n)
    if not ws:
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
                out.append((reg + k, v))
            i += 1 + c
        elif t == 7:
            op = (w >> 16) & 0x7F
            c = w & 0x7FFF
            body = ws[i+1:i+1+c]
            if op == 0x3f and c >= 3 and depth < 3:  # CP_INDIRECT_BUFFER
                tgt = body[0] | (body[1] << 32)
                sz = body[2] & 0xFFFF
                walk(bos, tgt, sz, out, depth + 1)
            i += 1 + c
        else:
            i += 1

WANT = [0xe590,0xe592,0xe593,0xe5a3,0xe280,0xe29d,0xe294,0xe282,0xe283,0xe284,0xe285,
        0xe5c0,0xe784,0xe785,0xe786,0xe787,0xe788,0xe585,0xe78c,0xe7d7,0xe7d8,0xe001,
        0xe5ca,0xe146,0xe147,0xe384]
NAMES = {0xe590:'SP_VS_CTRL_REG0',0xe592:'SP_PRIMITIVE_CNTL',0xe593:'SP_VS_OUT[0]',
         0xe5a3:'SP_VS_VPC_DST[0]',0xe280:'VPC_CNTL_0',0xe29d:'VPC_PACK',0xe294:'VPC_VAR[0]',
         0xe282:'VPC_VARYING_INTERP[0]',0xe285:'VPC_VARYING_INTERP[3]',
         0xe5c0:'SP_FS_CTRL_REG0',0xe784:'HLSQ_CONTROL_0',0xe785:'HLSQ_CONTROL_1',
         0xe786:'HLSQ_CONTROL_2',0xe787:'HLSQ_CONTROL_3',0xe788:'HLSQ_CONTROL_4',
         0xe585:'SP_FS_CONFIG',0xe78c:'HLSQ_FS_CONFIG',0xe7d7:'HLSQ_FS_CONSTLEN',
         0xe7d8:'HLSQ_FS_INSTRLEN',0xe001:'GRAS_VS_CL_CNTL',0xe5ca:'SP_FS_OUTPUT_CNTL',
         0xe146:'RB_FS_OUTPUT_CNTL',0xe147:'RB_RENDER_COMPONENTS',0xe384:'PC_PRIMITIVE_CNTL'}

def main(path, filter_comm=None):
    recs = parse_records(path)
    bos, cs, cmd = [], [], []
    submits = []   # (comm, list_of_[(iova,sz)])
    cur = None
    for typ, payload in recs:
        if typ == 2:  # RD_CMD text
            txt = payload.rstrip(b'\x00').decode('utf-8', 'replace')
            m = re.match(r'([^/]+)/(\d+): fence=(\d+)', txt)
            if m:
                cur = (m.group(1), m.group(2), m.group(3), [])
                submits.append(cur)
            continue
        if typ == 3:  # RD_GPUADDR
            iova, size, hi = struct.unpack('<III', payload[:12])
            bos.append([iova, size, None])
        elif typ == 12:  # RD_BUFFER_CONTENTS
            for b in reversed(bos):
                if b[2] is None:
                    b[2] = payload
                    break
        elif typ == 6:  # RD_CMDSTREAM_ADDR
            iova, szd, hi = struct.unpack('<III', payload[:12])
            if cur is not None:
                cur[3].append((iova, szd))
            else:
                cs.append((iova, szd))

    from collections import Counter
    print('=== KRD:', path, '=== submits:', len(submits))
    print('  %-20s %s' % ('comm/pid', 'fence'))
    for comm, pid, fence, streams in submits:
        print('  %-20s fence=%-6s streams=%d' % (comm + '/' + pid, fence, len(streams)))

    for comm, pid, fence, streams in submits:
        if filter_comm and filter_comm not in comm:
            continue
        # merge all streams of this submit, keep last-write-wins for registers
        out = {}
        order = []
        allw = []
        for iova, szd in streams:
            lo = []
            walk(bos, iova, szd, lo)
            allw.extend(lo)
            for reg, v in lo:
                if reg in WANT:
                    if reg not in out:
                        order.append(reg)
                    out[reg] = v
        if not out:
            continue
        print('  --- %s/%s fence=%s (total reg writes: %d) ---' % (comm, pid, fence, len(allw)))
        for reg in sorted(order):
            print('    0x%05x %-22s = 0x%08x' % (reg, NAMES.get(reg, '?'), out[reg]))
        if filter_comm and sys.argv[3:]:
            print('    -- full ordered stream (HLSQ/SP/VPC/GRAS/PC/RB) --')
            for reg, v in allw:
                if (0xe780 <= reg <= 0xe7e0) or (0xe580 <= reg <= 0xe5d0) or (0xe280 <= reg <= 0xe2a0) or reg in (0xe000,0xe001,0xe005,0xe006,0xe384,0xe388,0xe389,0xe0a0,0xe0a4,0xe0aa,0xe0ab,0xe146,0xe147):
                    print('      [%5x] = 0x%08x' % (reg, v))

if __name__ == '__main__':
    path = sys.argv[1]
    filt = sys.argv[2] if len(sys.argv) > 2 else None
    main(path, filt)
