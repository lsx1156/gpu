#!/usr/bin/env python3
"""手工编码 SPIR-V（无 glslangValidator 环境），生成 spirv_tri.h。

2×vec2 varying（方案1: 拆分颜色为多个 2 分量 varying）：
- VS: pos loc0(vec3)->gl_Position, 红绿 loc1(vec2)->VAR0(loc0), 蓝alpha loc2(vec2)->VAR1(loc1)
- FS: 读 VAR0(vec2)+VAR1(vec2)，用 OpVectorShuffle(0x7C) 组合输出 vec4(r,g,b,a)

关键：不能用 OpCompositeExtract(0x59) 对 varying 取分量（触发 Mesa vtn_base_type_image 误判）。
OpVectorShuffle 是安全的组合方式。

注意：id 严格按 s.id() 顺序分配，bound=最大id+1（<0x3FFFFF）。
"main" 字符串：ascii + NUL + 4 字节对齐。
"""

import struct

MAGIC = 0x07230203
VERSION = 0x00010000  # SPIR-V 1.0

class Spirv:
    def __init__(self):
        self.words = []
        self.bound = 1  # id 0 保留，从 1 开始分配

    def id(self):
        i = self.bound
        self.bound += 1
        return i

    def inst(self, opcode, operands):
        body = []
        for op in operands:
            if isinstance(op, bytes):
                b = op + b"\0" + b"\0" * ((4 - (len(op) + 1) % 4) % 4)
                for k in range(0, len(b), 4):
                    body.append(struct.unpack("<I", b[k:k+4])[0])
            else:
                body.append(op)
        self.words.append(((len(body) + 1) << 16) | opcode)
        self.words += body

    def header(self):
        return [MAGIC, VERSION, 0, self.bound, 0]


def build_vs():
    """VS: pos loc0 vec3, 红绿 loc1 vec2->VAR0, 蓝alpha loc2 vec2->VAR1"""
    s = Spirv()
    main_id = s.id()      # 1
    gl_pos = s.id()       # 2
    out_rg = s.id()       # 3  varying VAR0 (loc0)
    out_ba = s.id()       # 4  varying VAR1 (loc1)
    in_pos = s.id()       # 5  loc0 vec3
    in_rg = s.id()        # 6  loc1 vec2
    in_ba = s.id()        # 7  loc2 vec2

    t_void = s.id()       # 8
    t_fn = s.id()         # 9
    t_f32 = s.id()        # 10
    t_v2 = s.id()         # 11
    t_v3 = s.id()         # 12
    t_v4 = s.id()         # 13
    p_out_v4 = s.id()     # 14
    p_out_v2 = s.id()     # 15
    p_in_v3 = s.id()      # 16
    p_in_v2 = s.id()      # 17
    c_one = s.id()        # 18

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [0, main_id, b"main", gl_pos, out_rg, out_ba, in_pos, in_rg, in_ba])
    s.inst(0x0003, [1, 450])             # OpSource GLSL 450
    s.inst(0x0047, [gl_pos, 11, 0])      # OpDecorate gl_Position BuiltIn(Position)
    s.inst(0x0047, [in_pos, 30, 0])      # OpDecorate in_pos Location(0)
    s.inst(0x0047, [in_rg, 30, 1])       # OpDecorate in_rg Location(1)
    s.inst(0x0047, [in_ba, 30, 2])       # OpDecorate in_ba Location(2)
    s.inst(0x0047, [out_rg, 30, 0])      # OpDecorate out_rg Location(0) varying VAR0
    s.inst(0x0047, [out_ba, 30, 1])      # OpDecorate out_ba Location(1) varying VAR1

    s.inst(0x0013, [t_void])             # OpTypeVoid
    s.inst(0x0021, [t_fn, t_void])       # OpTypeFunction
    s.inst(0x0016, [t_f32, 32])          # OpTypeFloat 32
    s.inst(0x0017, [t_v2, t_f32, 2])     # OpTypeVector 2
    s.inst(0x0017, [t_v3, t_f32, 3])     # OpTypeVector 3
    s.inst(0x0017, [t_v4, t_f32, 4])     # OpTypeVector 4
    s.inst(0x0020, [p_out_v4, 3, t_v4])  # Output(vec4)
    s.inst(0x0020, [p_out_v2, 3, t_v2])  # Output(vec2)
    s.inst(0x0020, [p_in_v3, 1, t_v3])   # Input(vec3)
    s.inst(0x0020, [p_in_v2, 1, t_v2])   # Input(vec2)
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])  # OpConstant 1.0

    s.inst(0x003B, [p_out_v4, gl_pos, 3])   # OpVariable Output (Position)
    s.inst(0x003B, [p_out_v2, out_rg, 3])   # OpVariable Output (VAR0 vec2)
    s.inst(0x003B, [p_out_v2, out_ba, 3])   # OpVariable Output (VAR1 vec2)
    s.inst(0x003B, [p_in_v3, in_pos, 1])    # OpVariable Input (pos vec3)
    s.inst(0x003B, [p_in_v2, in_rg, 1])     # OpVariable Input (红绿 vec2)
    s.inst(0x003B, [p_in_v2, in_ba, 1])     # OpVariable Input (蓝alpha vec2)

    fn_id = main_id  # OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()      # 19
    pos = s.id()      # 20
    rg = s.id()       # 21
    ba = s.id()       # 22
    glp = s.id()      # 23
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])   # OpFunction
    s.inst(0x00F8, [lbl])                      # OpLabel
    s.inst(0x003D, [t_v3, pos, in_pos])        # OpLoad pos
    s.inst(0x003D, [t_v2, rg, in_rg])          # OpLoad 红绿
    s.inst(0x003D, [t_v2, ba, in_ba])          # OpLoad 蓝alpha
    s.inst(0x003E, [out_rg, rg])               # OpStore VAR0
    s.inst(0x003E, [out_ba, ba])               # OpStore VAR1
    s.inst(0x0050, [t_v4, glp, pos, c_one])    # OpCompositeConstruct v4(pos, 1.0)
    s.inst(0x003E, [gl_pos, glp])              # OpStore gl_Position
    s.inst(0x00FD, [])                         # OpReturn
    s.inst(0x0038, [])                         # OpFunctionEnd
    return s


def build_fs():
    """FS: 读 VAR0(vec2)+VAR1(vec2)，OpVectorShuffle 组合输出 vec4(r,g,b,a)"""
    s = Spirv()
    main_id = s.id()      # 1
    out_col = s.id()      # 2
    in_rg = s.id()        # 3
    in_ba = s.id()        # 4

    t_void = s.id()       # 5
    t_fn = s.id()         # 6
    t_f32 = s.id()        # 7
    t_v2 = s.id()         # 8
    t_v4 = s.id()         # 9
    p_out_v4 = s.id()     # 10
    p_in_v2 = s.id()      # 11

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [4, main_id, b"main", out_col, in_rg, in_ba])
    s.inst(0x0010, [main_id, 7])         # OpExecutionMode OriginUpperLeft (=7)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate out_col Location(0)
    s.inst(0x0047, [in_rg, 30, 0])       # OpDecorate in_rg Location(0) VAR0
    s.inst(0x0047, [in_ba, 30, 1])       # OpDecorate in_ba Location(1) VAR1

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v2, t_f32, 2])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v2, 1, t_v2])

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v2, in_rg, 1])
    s.inst(0x003B, [p_in_v2, in_ba, 1])

    fn_id = main_id  # OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()      # 12
    rg = s.id()       # 13
    ba = s.id()       # 14
    cv = s.id()       # 15
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v2, rg, in_rg])        # OpLoad VAR0
    s.inst(0x003D, [t_v2, ba, in_ba])        # OpLoad VAR1
    # OpCompositeConstruct v4 cv, rg, ba (vec2+vec2->vec4, 绕开 Shuffle/Extract 的 Mesa 误判)
    s.inst(0x0050, [t_v4, cv, rg, ba])
    s.inst(0x003E, [out_col, cv])            # OpStore out_col
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def emit_c(name, s):
    ws = s.header() + s.words
    lines = [f"static const uint32_t {name}[] = {{\n"]
    for i in range(0, len(ws), 6):
        lines.append("    " + ", ".join(f"0x{w:08x}u" for w in ws[i:i+6]) + ",\n")
    lines.append("};\n")
    return "".join(lines)


hdr = """/* spirv_tri.h - 由 gen_spirv.py 生成，勿手改 */
#include <stdint.h>

"""
vs = build_vs()
fs = build_fs()
out = hdr + emit_c("vs_spv", vs) + "\n" + emit_c("fs_spv", fs) + "\n"
with open(__file__.rsplit("\\", 1)[0] + r"\spirv_tri.h", "w", newline="\n") as f:
    f.write(out)
print(f"VS bound: {vs.header()[3]}, words: {len(vs.header() + vs.words)}")
print(f"FS bound: {fs.header()[3]}, words: {len(fs.header() + fs.words)}")
print("spirv_tri.h written")