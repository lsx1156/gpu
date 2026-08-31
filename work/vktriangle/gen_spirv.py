#!/usr/bin/env python3
"""手工编码 SPIR-V（无 glslangValidator 环境），生成 spirv_tri.h。

VS: 每顶点读 location0 的 vec3 顶点位置 -> gl_Position (w=1)
FS: location0 输出常量绿色 (0,1,0,1)
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
        # operands: int(id/常量) 或 bytes(字面字符串，NUL 终止后按 4 字节对齐)
        # WordCount 必须按实际字数计算（字符串可占多个 word）
        body = []
        for op in operands:
            if isinstance(op, bytes):
                # SPIR-V 字符串必须 NUL 终止后再按 4 字节对齐
                b = op + b"\0" + b"\0" * ((4 - (len(op) + 1) % 4) % 4)
                for k in range(0, len(b), 4):
                    body.append(struct.unpack("<I", b[k:k+4])[0])
            else:
                body.append(op)
        self.words.append(((len(body) + 1) << 16) | opcode)
        self.words += body

    def header(self):
        return [MAGIC, VERSION, 0, self.bound, 0]


def opcode_word_count_fix(opc, ops):
    pass

def build_vs():
    s = Spirv()
    main_id = s.id()
    gl_pos = s.id()
    in_pos = s.id()

    t_void = s.id()
    t_fn = s.id()
    t_f32 = s.id()
    t_v3 = s.id()
    t_v4 = s.id()
    p_out_v4 = s.id()
    p_in_v3 = s.id()
    c_one = s.id()

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    # OpEntryPoint Vertex(main) "main" gl_Position in_pos
    s.inst(0x000F, [0, main_id, b"main", gl_pos, in_pos])
    s.inst(0x0003, [1, 450])             # OpSource GLSL 450
    s.inst(0x0047, [gl_pos, 11, 0])      # OpDecorate gl_Position BuiltIn(Position)
    s.inst(0x0047, [in_pos, 30, 0])      # OpDecorate in_pos Location(0)  (Location=30)

    s.inst(0x0013, [t_void])             # OpTypeVoid
    s.inst(0x0021, [t_fn, t_void])       # OpTypeFunction
    s.inst(0x0016, [t_f32, 32])          # OpTypeFloat 32
    s.inst(0x0017, [t_v3, t_f32, 3])     # OpTypeVector 3
    s.inst(0x0017, [t_v4, t_f32, 4])     # OpTypeVector 4
    s.inst(0x0020, [p_out_v4, 3, t_v4])  # OpTypePointer Output
    s.inst(0x0020, [p_in_v3, 1, t_v3])   # OpTypePointer Input
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])  # OpConstant 1.0 (type, id, value)

    s.inst(0x003B, [p_out_v4, gl_pos, 3])  # OpVariable (type, id, storage)
    s.inst(0x003B, [p_in_v3, in_pos, 1])   # OpVariable Input

    fn_id = main_id  # SPIR-V: OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()
    pos = s.id()
    glp = s.id()
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])   # OpFunction (type, id, ctrl, ftype)
    s.inst(0x00F8, [lbl])                      # OpLabel
    s.inst(0x003D, [t_v3, pos, in_pos])        # OpLoad (type, id, ptr)
    # OpCompositeConstruct v4(pos, 1.0)  (opcode 0x50 = 80)
    s.inst(0x0050, [t_v4, glp, pos, c_one])
    s.inst(0x003E, [gl_pos, glp])              # OpStore
    s.inst(0x00FD, [])                         # OpReturn
    s.inst(0x0038, [])                         # OpFunctionEnd
    return s


def build_fs():
    s = Spirv()
    main_id = s.id()
    out_col = s.id()

    t_void = s.id()
    t_fn = s.id()
    t_f32 = s.id()
    t_v4 = s.id()
    p_out_v4 = s.id()
    c_zero = s.id()
    c_one = s.id()

    s.inst(0x0011, [1])                  # OpCapability Shader
    s.inst(0x000E, [0, 1])               # OpMemoryModel Logical GLSL450
    s.inst(0x000F, [4, main_id, b"main", out_col])  # OpEntryPoint Fragment
    s.inst(0x0010, [main_id, 7])         # OpExecutionMode OriginUpperLeft (=7)
    s.inst(0x0047, [out_col, 30, 0])     # OpDecorate Location(0)  (Location=30)

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x002B, [t_f32, c_zero, 0x00000000])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, out_col, 3])

    fn_id = main_id  # SPIR-V: OpEntryPoint 的 id 必须就是入口函数的 result id
    lbl = s.id()
    col = s.id()
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    # OpCompositeConstruct v4(0, 1, 0, 1)  (opcode 0x50 = 80)
    s.inst(0x0050, [t_v4, col, c_zero, c_one, c_zero, c_one])
    s.inst(0x003E, [out_col, col])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def emit_c(name, s):
    ws = s.header() + s.words
    lines = [f"static const uint32_t {name}[] = {{"]
    for i in range(0, len(ws), 6):
        lines.append("    " + ", ".join(f"0x{w:08x}u" for w in ws[i:i+6]) + ",")
    lines.append("};")
    return "\n".join(lines)


hdr = """/* spirv_tri.h - 由 gen_spirv.py 生成，勿手改 */
#include <stdint.h>

"""
out = hdr + emit_c("vs_spv", build_vs()) + "\n\n" + emit_c("fs_spv", build_fs()) + "\n"
with open(__file__.rsplit("\\", 1)[0] + r"\spirv_tri.h", "w", newline="\n") as f:
    f.write(out)
print("spirv_tri.h written")
