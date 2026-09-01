#!/usr/bin/env python3
"""手工编码 SPIR-V（无 glslangValidator 环境），生成 spirv_tri.h。

VS: 每顶点读 location0 的 vec3 顶点位置 -> gl_Position (w=1)
    location1 vec3 顶点颜色 -> 透传为 location0 varying vec2(xy) (逐顶点插值)
FS: location0 读入 varying 顶点颜色 vec2(xy) -> location0 输出 vec4(xy, 0, 1)
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
    """VS: location0 vec3 顶点位置 -> gl_Position (w=1)
    location1 vec3 顶点颜色 -> 透传为 location0 varying vec2(xy) (逐顶点插值)
    
    关键：从 uniform base=2 (x), base=6 (y) 分别 load，构造 vec2
    """
    s = Spirv()
    main_id = s.id()
    gl_pos = s.id()
    out_col = s.id()
    in_pos = s.id()
    in_col = s.id()

    t_void = s.id()
    t_fn = s.id()
    t_f32 = s.id()
    t_v2 = s.id()  # vec2 for varying
    t_v3 = s.id()
    t_v4 = s.id()
    p_out_v4 = s.id()
    p_out_v2 = s.id()
    p_in_v3 = s.id()
    c_one = s.id()

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [0, main_id, b"main", gl_pos, out_col, in_pos, in_col])
    s.inst(0x0003, [1, 450])             # OpSource GLSL 450
    s.inst(0x0047, [gl_pos, 11, 0])      # OpDecorate gl_Position BuiltIn(Position)
    s.inst(0x0047, [in_pos, 30, 0])      # OpDecorate in_pos Location(0)
    s.inst(0x0047, [in_col, 30, 1])      # OpDecorate in_col Location(1)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate out_col Location(0)

    s.inst(0x0013, [t_void])             # OpTypeVoid
    s.inst(0x0021, [t_fn, t_void])       # OpTypeFunction
    s.inst(0x0016, [t_f32, 32])          # OpTypeFloat 32
    s.inst(0x0017, [t_v2, t_f32, 2])     # OpTypeVector 2 for varying
    s.inst(0x0017, [t_v3, t_f32, 3])     # OpTypeVector 3
    s.inst(0x0017, [t_v4, t_f32, 4])     # OpTypeVector 4
    s.inst(0x0020, [p_out_v4, 3, t_v4])  # OpTypePointer Output
    s.inst(0x0020, [p_out_v2, 3, t_v2])  # OpTypePointer Output(vec2)
    s.inst(0x0020, [p_in_v3, 1, t_v3])   # OpTypePointer Input(vec3)
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])  # OpConstant 1.0

    s.inst(0x003B, [p_out_v4, gl_pos, 3])   # OpVariable Output (Position)
    s.inst(0x003B, [p_out_v2, out_col, 3])  # OpVariable Output (varying color vec2)
    s.inst(0x003B, [p_in_v3, in_pos, 1])    # OpVariable Input
    s.inst(0x003B, [p_in_v3, in_col, 1])    # OpVariable Input

    fn_id = main_id
    lbl = s.id()
    pos = s.id()
    col = s.id()
    glp = s.id()
    x = s.id()
    y = s.id()
    xy = s.id()
    
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])   # OpFunction
    s.inst(0x00F8, [lbl])                      # OpLabel
    s.inst(0x003D, [t_v3, pos, in_pos])        # OpLoad pos (vec3)
    
    # 从 uniform base=2 load x, base=6 load y (对应 VS 输入 in_col 的 x,y)
    s.inst(0x003D, [t_f32, x, in_col])         # OpLoad x (base=2)
    s.inst(0x003D, [t_f32, y, in_col])         # OpLoad y (base=6) - 需要 offset
    
    # 实际上 SPIR-V 的 OpLoad 对数组/结构体有 offset，但对于 uniform 我们用不同的变量
    # 更简单的方法：用 OpAccessChain + OpLoad，但我们的 uniform 是数组
    # 这里用两个独立的 uniform 变量：in_col_x 和 in_col_y
    # 但为了复用现有 uniform，用 OpCompositeExtract
    s.inst(0x003D, [t_v3, col, in_col])        # OpLoad color vec3
    s.inst(0x0059, [t_f32, x, col, 0])         # OpCompositeExtract x (component 0)
    s.inst(0x0059, [t_f32, y, col, 1])         # OpCompositeExtract y (component 1)
    
    # 构造 vec2(x, y)
    s.inst(0x0050, [t_v2, xy, x, y])           # OpCompositeConstruct vec2
    s.inst(0x003E, [out_col, xy])              # OpStore out_color (varying vec2)
    
    # gl_Position
    s.inst(0x003D, [t_v3, pos, in_pos])        # OpLoad pos
    s.inst(0x0050, [t_v4, glp, pos, c_one])    # OpCompositeConstruct vec4(pos, 1.0)
    s.inst(0x003E, [gl_pos, glp])              # OpStore gl_Position
    s.inst(0x00FD, [])                         # OpReturn
    s.inst(0x0038, [])                         # OpFunctionEnd
    return s


def build_fs():
    """FS: location0 读入 varying 顶点颜色 vec2(xy) -> location0 输出 vec4(xy, 0, 1)"""
    s = Spirv()
    main_id = s.id()
    out_col = s.id()
    in_col = s.id()

    t_void = s.id()
    t_fn = s.id()
    t_f32 = s.id()
    t_v2 = s.id()  # vec2 for varying
    t_v4 = s.id()
    p_out_v4 = s.id()
    p_in_v2 = s.id()
    c_zero = s.id()
    c_one = s.id()

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col])
    s.inst(0x0010, [main_id, 7])         # OpExecutionMode OriginUpperLeft (=7)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate out_col Location(0)
    s.inst(0x0047, [in_col, 30, 0])      # OpDecorate in_col Location(0)

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v2, t_f32, 2])     # OpTypeVector 2
    s.inst(0x0017, [t_v4, t_f32, 4])     # OpTypeVector 4
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v2, 1, t_v2])
    s.inst(0x002B, [t_f32, c_zero, 0x00000000])  # OpConstant 0.0
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])   # OpConstant 1.0

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v2, in_col, 1])

    fn_id = main_id
    lbl = s.id()
    col = s.id()
    cv = s.id()
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v2, col, in_col])          # OpLoad in_color (varying, vec2)
    s.inst(0x0050, [t_v4, cv, col, c_zero, c_one])  # OpCompositeConstruct vec4(xy, 0, 1)
    s.inst(0x003E, [out_col, cv])                # OpStore out_col
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
out = hdr + emit_c("vs_spv", build_vs()) + "\n" + emit_c("fs_spv", build_fs()) + "\n"
with open(__file__.rsplit("\\", 1)[0] + r"\spirv_tri.h", "w", newline="\n") as f:
    f.write(out)
print("spirv_tri.h written")