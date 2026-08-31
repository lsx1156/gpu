#!/usr/bin/env python3
"""解析 msm devcoredump：ascii85 解码 BO，逐包解析 PM4 命令流（a5xx）。
用法: python parse_devcd.py devcd.txt [iova]
"""
import re, sys

# adreno_pm4 type3 opcodes (a5xx 有效子集)
T3 = {
    16: "CP_NOP", 19: "CP_WAIT_FOR_ME", 18: "CP_WAIT_MEM_WRITES", 20: "CP_WAIT_MEM_GTE",
    28: "CP_PREEMPT_ENABLE", 29: "CP_SKIP_IB2_ENABLE_GLOBAL", 30: "CP_PREEMPT_TOKEN",
    33: "CP_REG_RMW", 34: "CP_DRAW_INDX", 35: "CP_VIZ_QUERY", 36: "CP_DRAW_AUTO",
    37: "CP_SET_STATE", 38: "CP_WAIT_FOR_IDLE", 39: "CP_IM_LOAD", 40: "CP_DRAW_INDIRECT",
    41: "CP_DRAW_INDX_INDIRECT", 42: "CP_DRAW_INDIRECT_MULTI", 43: "CP_IM_LOAD_IMMEDIATE",
    44: "CP_BLIT", 45: "CP_SET_CONSTANT", 46: "CP_SET_BIN_DATA5_OFFSET", 47: "CP_SET_BIN_DATA5",
    48: "CP_LOAD_STATE4", 49: "CP_RUN_OPENCL", 50: "CP_LOAD_STATE6_GEOM", 51: "CP_EXEC_CS",
    52: "CP_LOAD_STATE6_FRAG", 53: "CP_SET_SUBDRAW_SIZE", 54: "CP_LOAD_STATE6",
    55: "CP_INDIRECT_BUFFER_PFD", 56: "CP_DRAW_INDX_OFFSET", 57: "CP_REG_TEST",
    58: "CP_COND_INDIRECT_BUFFER_PFE", 59: "CP_INVALIDATE_STATE", 60: "CP_WAIT_REG_MEM",
    61: "CP_MEM_WRITE", 62: "CP_REG_TO_MEM", 63: "CP_INDIRECT_BUFFER", 64: "CP_INTERRUPT",
    65: "CP_EXEC_CS_INDIRECT", 66: "CP_MEM_TO_REG", 67: "CP_SET_DRAW_STATE",
    68: "CP_COND_EXEC", 69: "CP_COND_WRITE5", 70: "CP_EVENT_WRITE", 71: "CP_COND_REG_EXEC",
    72: "CP_ME_INIT", 74: "CP_REG_TO_SCRATCH", 75: "CP_SET_DRAW_INIT_FLAGS", 76: "CP_SCRATCH_WRITE",
    77: "CP_SCRATCH_TO_REG", 78: "CP_DRAW_PRED_SET", 79: "CP_MEM_WRITE_CNTR",
    80: "CP_SET_BIN_MASK", 81: "CP_SET_BIN_SELECT", 82: "CP_WAIT_REG_EQ", 83: "CP_SMMU_TABLE_UPDATE",
    84: "CP_CONTEXT_SWITCH", 85: "CP_SET_CTXSWITCH_IB", 86: "CP_SET_PSEUDO_REG",
    87: "CP_INDIRECT_BUFFER_CHAIN", 88: "CP_EVENT_WRITE_SHD", 89: "CP_EVENT_WRITE_CFL",
    91: "CP_EVENT_WRITE_ZPD", 92: "CP_CONTEXT_REG_BUNCH", 93: "CP_WAIT_IB_PFD_COMPLETE",
    94: "CP_CONTEXT_UPDATE", 95: "CP_SET_PROTECTED_MODE", 98: "CP_WHERE_AM_I",
    99: "CP_SET_MODE", 100: "CP_SET_VISIBILITY_OVERRIDE", 101: "CP_SET_MARKER",
    102: "CP_SET_SECURE_MODE", 105: "CP_PREEMPT_ENABLE_GLOBAL", 106: "CP_PREEMPT_ENABLE_LOCAL",
    107: "CP_CONTEXT_SWITCH_YIELD", 108: "CP_SET_RENDER_MODE", 109: "CP_REG_WRITE",
    110: "CP_COMPUTE_CHECKPOINT", 111: "CP_BOOTSTRAP_UCODE", 112: "CP_WAIT_TWO_REGS",
    113: "CP_TEST_TWO_MEMS", 114: "CP_REG_TO_MEM_OFFSET_REG", 115: "CP_MEM_TO_MEM",
    116: "CP_WIDE_REG_WRITE", 117: "CP_MEMCPY",
}
# 需要跳过的伪 opcode（GPU 微码私有指令区间）
VALID = set(T3) | {23, 31, 32, 73, 10, 11, 15, 17, 25, 26, 58, 92}

def ascii85_decode(line, nwords):
    buf = []
    i = 0
    s = line.strip()
    while i < len(s) and len(buf) < nwords:
        c = s[i]
        if c == 'z':
            buf.append(0); i += 1; continue
        if c < '!' or c > 'u':
            break
        accum, j = 0, 0
        while j < 5 and i < len(s) and '!' <= s[i] <= 'u':
            accum = accum * 85 + (ord(s[i]) - 33); i += 1; j += 1
        buf.append(accum & 0xffffffff)
    return buf[:nwords]

def parse_devcd(path):
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    bos = {}
    cur = None
    for idx, ln in enumerate(lines):
        m = re.search(r'iova:\s*(0x[0-9a-fA-F]+)', ln)
        if m:
            cur = int(m.group(1), 16)
            bos[cur] = []
            continue
        if 'data: !!ascii85' in ln and cur is not None:
            # 数据行是下一行
            for j in range(idx + 1, min(idx + 5, len(lines))):
                if lines[j].strip():
                    print(f"[raw {cur:#x}] {lines[j][:70]!r}")
                    bos[cur] = ascii85_decode(lines[j], 10 ** 9)
                    break
    return bos

def walk(dwords, label, maxpk=2000):
    print(f"==== {label}: {len(dwords)} dwords ====")
    i = 0
    npk = 0
    while i < len(dwords) and npk < maxpk:
        w = dwords[i]
        if (w >> 28) == 0x7:  # TYPE7: opcode[22:16], cnt[14:0]
            cnt = w & 0x7FFF
            op = (w >> 16) & 0x7F
            name = T3.get(op, f"OP_{op}")
            body = dwords[i + 1:i + 1 + cnt]
            print(f"[{i:5d}] T7 {name}({op}) cnt={cnt} body={ ' '.join(f'{x:08x}' for x in body[:12]) }{' ...' if cnt>12 else ''}")
            if op == 63 and cnt >= 2:  # CP_INDIRECT_BUFFER
                iova = (body[1] << 32) | body[0]
                print(f"      -> IB @ {iova:#x} size={body[2] if cnt>=3 else '?'} dwords")
            i += 1 + cnt
        elif (w >> 30) == 0b01:  # TYPE4: cnt[6:0], reg[25:8]
            cnt = w & 0x7F
            reg = (w >> 8) & 0x3FFFF
            body = dwords[i + 1:i + 1 + cnt]
            print(f"[{i:5d}] T4 reg={reg:#06x} cnt={cnt} vals=" + ' '.join(f'{x:08x}' for x in body[:12]))
            i += 1 + cnt
        elif (w >> 30) == 0b11:  # TYPE3
            cnt = ((w >> 16) & 0x7FFF) + 1
            op = (w >> 8) & 0xFF
            name = T3.get(op, f"OP_{op}")
            body = dwords[i + 1:i + 1 + cnt]
            print(f"[{i:5d}] T3 {name}({op}) cnt={cnt} body={ ' '.join(f'{x:08x}' for x in body[:12]) }{' ...' if cnt>12 else ''}")
            if op == 63 and cnt >= 2:
                iova = (body[1] << 32) | body[0]
                print(f"      -> IB @ {iova:#x} size={body[2] if cnt>=3 else '?'} dwords")
            i += 1 + cnt
        elif (w >> 30) == 0b10:
            print(f"[{i:5d}] T2 0x{w:08x}")
            i += 1
        else:
            print(f"[{i:5d}] ?? 0x{w:08x} <-- 失步?")
            i += 1
        npk += 1

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else 'devcd.txt'
    want = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    off = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    n = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0
    bos = parse_devcd(path)
    if want and want in bos:
        if n:
            walk(bos[want][off:off + n], f"BO {want:#x} off={off} n={n}")
        else:
            walk(bos[want], f"BO {want:#x}")
    else:
        print("BOs:", [f"{k:#x} ({len(v)} dw)" for k, v in bos.items()])
        for k, v in bos.items():
            if len(v) and all(x == 0 for x in v[:64]):
                continue
            walk(v, f"BO {k:#x}", maxpk=300)
