#!/usr/bin/env python3
"""实验用 SPIR-V 生成器（H2b 蓝分量隔离实验）。

mode=fs_const_blue : FS 直接输出常量 vec4(0,0,1,1)，完全不读 varying。
                     -> 若蓝通道出现，说明 FS 输出 + 读回路径对 comp2 正常。
mode=vs_const_blue : VS 仍读顶点颜色，但把 varying 的第 3 分量强制为 1.0。
                     -> 若蓝仍为 0，说明 VPC/FS 侧 comp2 递送/读取有问题；
                        若蓝为满值，说明问题在顶点属性第 3 分量的取数。
用法: python gen_spirv_exp.py <mode> <输出路径>
"""
import struct
import sys

MAGIC = 0x07230203
VERSION = 0x00010000


class Spirv:
    def __init__(self):
        self.words = []
        self.bound = 1

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
                    body.append(struct.unpack("<I", b[k:k + 4])[0])
            else:
                body.append(op)
        self.words.append(((len(body) + 1) << 16) | opcode)
        self.words += body

    def header(self):
        return [MAGIC, VERSION, 0, self.bound, 0]


def build_vs_const_blue():
    """VS: pos(vec3,loc0) + col(vec3,loc1) -> gl_Position, varying(loc0)=vec3(col.x,col.y,1.0)"""
    s = Spirv()
    main_id = s.id()
    gl_pos = s.id()
    out_col = s.id()
    in_pos = s.id()
    in_col = s.id()

    t_void = s.id(); t_fn = s.id(); t_f32 = s.id()
    t_v3 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id(); p_out_v3 = s.id(); p_in_v3 = s.id()
    c_zero = s.id(); c_one = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [0, main_id, b"main", gl_pos, out_col, in_pos, in_col])
    s.inst(0x0003, [1, 450])
    s.inst(0x0047, [gl_pos, 11, 0])       # BuiltIn Position
    s.inst(0x0047, [in_pos, 30, 0])       # vertex input loc0
    s.inst(0x0047, [in_col, 30, 1])       # vertex input loc1
    s.inst(0x0047, [out_col, 30, 0])      # varying output loc0

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_out_v3, 3, t_v3])
    s.inst(0x0020, [p_in_v3, 1, t_v3])
    s.inst(0x002B, [t_f32, c_zero, 0x00000000])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, gl_pos, 3])
    s.inst(0x003B, [p_out_v3, out_col, 3])
    s.inst(0x003B, [p_in_v3, in_pos, 1])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    lbl = s.id(); pos = s.id(); col = s.id(); glp = s.id()
    cx = s.id(); cy = s.id(); ncol = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, pos, in_pos])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x0051, [t_f32, cx, col, 0])            # OpCompositeExtract x
    s.inst(0x0051, [t_f32, cy, col, 1])            # OpCompositeExtract y
    s.inst(0x0050, [t_v3, ncol, cx, cy, c_one])    # vec3(col.x, col.y, 1.0)
    s.inst(0x003E, [out_col, ncol])
    s.inst(0x0050, [t_v4, glp, pos, c_one])
    s.inst(0x003E, [gl_pos, glp])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_const_blue():
    """FS: 不读任何 varying，直接输出常量 vec4(0,0,1,1)"""
    s = Spirv()
    main_id = s.id()
    out_col = s.id()

    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id()
    c_zero = s.id(); c_one = s.id(); c_col = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out_col])
    s.inst(0x0010, [main_id, 7])          # OriginUpperLeft
    s.inst(0x0047, [out_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x002B, [t_f32, c_zero, 0x00000000])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])
    s.inst(0x002C, [t_v4, c_col, c_zero, c_zero, c_one, c_one])  # OpConstantComposite

    s.inst(0x003B, [p_out_v4, out_col, 3])

    lbl = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003E, [out_col, c_col])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_from_base():
    """FS: 原样（读 varying loc0 的 vec3 -> 输出 vec4(col,1)）"""
    s = Spirv()
    main_id = s.id(); out_col = s.id(); in_col = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id(); p_in_v3 = s.id(); c_one = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out_col, 30, 0])
    s.inst(0x0047, [in_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v3, 1, t_v3])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    lbl = s.id(); col = s.id(); cv = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x0050, [t_v4, cv, col, c_one])
    s.inst(0x003E, [out_col, cv])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_vs_base():
    """VS: 原样（pos loc0 -> gl_Position, col loc1 -> varying loc0）"""
    s = Spirv()
    main_id = s.id(); gl_pos = s.id(); out_col = s.id()
    in_pos = s.id(); in_col = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id(); p_out_v3 = s.id(); p_in_v3 = s.id(); c_one = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [0, main_id, b"main", gl_pos, out_col, in_pos, in_col])
    s.inst(0x0003, [1, 450])
    s.inst(0x0047, [gl_pos, 11, 0])
    s.inst(0x0047, [in_pos, 30, 0])
    s.inst(0x0047, [in_col, 30, 1])
    s.inst(0x0047, [out_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_out_v3, 3, t_v3])
    s.inst(0x0020, [p_in_v3, 1, t_v3])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, gl_pos, 3])
    s.inst(0x003B, [p_out_v3, out_col, 3])
    s.inst(0x003B, [p_in_v3, in_pos, 1])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    lbl = s.id(); pos = s.id(); col = s.id(); glp = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, pos, in_pos])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x003E, [out_col, col])
    s.inst(0x0050, [t_v4, glp, pos, c_one])
    s.inst(0x003E, [gl_pos, glp])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def emit_c(name, s):
    ws = s.header() + s.words
    lines = ["static const uint32_t %s[] = {" % name]
    for i in range(0, len(ws), 6):
        lines.append("    " + ", ".join("0x%08xu" % w for w in ws[i:i + 6]) + ",")
    lines.append("};")
    return "\n".join(lines)


MODES = {
    'fs_const_blue': (build_vs_base, build_fs_const_blue),
    'vs_const_blue': (build_vs_const_blue, build_fs_from_base),
    'base': (build_vs_base, build_fs_from_base),
}

if __name__ == '__main__':
    mode = sys.argv[1]
    out = sys.argv[2]
    vsf, fsf = MODES[mode]
    txt = ("/* spirv_tri.h - gen_spirv_exp.py mode=%s */\n#include <stdint.h>\n\n" % mode
           + emit_c("vs_spv", vsf()) + "\n\n" + emit_c("fs_spv", fsf()) + "\n")
    with open(out, "w", newline="\n") as f:
        f.write(txt)
    print("wrote %s (mode=%s)" % (out, mode))
