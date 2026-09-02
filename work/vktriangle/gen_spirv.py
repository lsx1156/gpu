#!/usr/bin/env python3
"""手工编码 SPIR-V（无 glslangValidator 环境），生成 spirv_tri.h。

vec4 颜色 varying（当前验证版）：
- VS: location0 vec3 顶点位置 -> gl_Position(w=1)
      location1 vec4 顶点颜色 -> 透传为 location0 varying(vec4)，逐顶点插值
- FS: location0 读入 varying 顶点颜色(vec4) -> location0 输出 vec4(rgba)

当前实测（sentinel 修复后驱动）：
- 红/绿分量（bary.f inloc 0,1）完全正确
- 蓝/alpha 分量（bary.f inloc 2,3）读 0 —— A5XX varying >2 分量问题，待查

注意：id 严格按 s.id() 顺序分配，bound=最大id+1（<0x3FFFFF）。
"main" 字符串：ascii + NUL + 4 字节对齐。
不要用 OpCompositeExtract 对 varying 取分量（触发 Mesa vtn_base_type_image 误判）。
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
    """VS: location0 vec3 顶点位置 -> gl_Position(w=1)
    location1 vec4 顶点颜色 -> 透传为 location0 varying(vec4)，逐顶点插值"""
    s = Spirv()
    main_id = s.id()      # 1
    gl_pos = s.id()       # 2  BuiltIn Position 输出
    out_col = s.id()      # 3  varying 颜色输出 location 0
    in_pos = s.id()       # 4  顶点位置输入 location 0
    in_col = s.id()       # 5  顶点颜色输入 location 1

    t_void = s.id()       # 6
    t_fn = s.id()         # 7
    t_f32 = s.id()        # 8
    t_v3 = s.id()         # 9
    t_v4 = s.id()         # 10
    p_out_v4 = s.id()     # 11
    p_out_v4c = s.id()    # 12 Output(vec4) 颜色
    p_in_v3 = s.id()      # 13 Input(vec3) pos
    p_in_v4c = s.id()     # 14 Input(vec4) 颜色
    c_one = s.id()        # 15

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [0, main_id, b"main", gl_pos, out_col, in_pos, in_col])
    s.inst(0x0003, [1, 450])             # OpSource GLSL 450
    s.inst(0x0047, [gl_pos, 11, 0])      # OpDecorate gl_Position BuiltIn(Position)
    s.inst(0x0047, [in_pos, 30, 0])      # OpDecorate in_pos Location(0)
    s.inst(0x0047, [in_col, 30, 1])      # OpDecorate in_col Location(1) (顶点输入)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate out_col Location(0) (varying 输出)

    s.inst(0x0013, [t_void])             # OpTypeVoid
    s.inst(0x0021, [t_fn, t_void])       # OpTypeFunction
    s.inst(0x0016, [t_f32, 32])          # OpTypeFloat 32
    s.inst(0x0017, [t_v3, t_f32, 3])     # OpTypeVector 3
    s.inst(0x0017, [t_v4, t_f32, 4])     # OpTypeVector 4
    s.inst(0x0020, [p_out_v4, 3, t_v4])  # OpTypePointer Output(vec4) pos
    s.inst(0x0020, [p_out_v4c, 3, t_v4]) # OpTypePointer Output(vec4) color
    s.inst(0x0020, [p_in_v3, 1, t_v3])   # OpTypePointer Input(vec3)
    s.inst(0x0020, [p_in_v4c, 1, t_v4])  # OpTypePointer Input(vec4)
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])  # OpConstant 1.0

    s.inst(0x003B, [p_out_v4, gl_pos, 3])   # OpVariable Output (Position)
    s.inst(0x003B, [p_out_v4c, out_col, 3]) # OpVariable Output (varying color vec4)
    s.inst(0x003B, [p_in_v3, in_pos, 1])    # OpVariable Input (pos vec3)
    s.inst(0x003B, [p_in_v4c, in_col, 1])   # OpVariable Input (color vec4)

    fn_id = main_id  # OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()      # 16
    pos = s.id()      # 17
    col = s.id()      # 18
    glp = s.id()      # 19
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])   # OpFunction
    s.inst(0x00F8, [lbl])                      # OpLabel
    s.inst(0x003D, [t_v3, pos, in_pos])        # OpLoad pos (vec3)
    s.inst(0x003D, [t_v4, col, in_col])        # OpLoad color (vec4)
    s.inst(0x003E, [out_col, col])             # OpStore out_color (varying vec4)
    s.inst(0x0050, [t_v4, glp, pos, c_one])    # OpCompositeConstruct v4(pos, 1.0)
    s.inst(0x003E, [gl_pos, glp])              # OpStore gl_Position
    s.inst(0x00FD, [])                         # OpReturn
    s.inst(0x0038, [])                         # OpFunctionEnd
    return s


def build_fs():
    """FS: location0 读入 varying 顶点颜色(vec4) -> location0 输出 vec4(rgba)"""
    s = Spirv()
    main_id = s.id()      # 1
    out_col = s.id()      # 2
    in_col = s.id()       # 3

    t_void = s.id()       # 4
    t_fn = s.id()         # 5
    t_f32 = s.id()        # 6
    t_v4 = s.id()         # 7
    p_out_v4 = s.id()     # 8
    p_in_v4 = s.id()      # 9

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col])
    s.inst(0x0010, [main_id, 7])         # OpExecutionMode OriginUpperLeft (=7)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate out_col Location(0)
    s.inst(0x0047, [in_col, 30, 0])      # OpDecorate in_col Location(0)

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v4, 1, t_v4])

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v4, in_col, 1])

    fn_id = main_id  # OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()      # 10
    col = s.id()      # 11
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v4, col, in_col])     # OpLoad in_color (varying vec4)
    s.inst(0x003E, [out_col, col])          # OpStore out_col (vec4)
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