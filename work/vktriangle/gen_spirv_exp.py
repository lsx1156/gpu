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


def build_fs_alpha_half():
    """FS: 读 varying -> vec4(col.xyz, 0.5)  (验 alpha 写入)"""
    s = Spirv()
    main_id = s.id(); out_col = s.id(); in_col = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id(); p_in_v3 = s.id(); c_half = s.id()

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
    s.inst(0x002B, [t_f32, c_half, 0x3F000000])

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    lbl = s.id(); col = s.id(); cv = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x0050, [t_v4, cv, col, c_half])
    s.inst(0x003E, [out_col, cv])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_mrt2():
    """FS: 两个颜色输出
    loc0 = vec4(col.rgb, 1.0)
    loc1 = vec4(col.b, col.g, col.r, 1.0)   (R/B 置换 -- 用于鉴别 MRT 路由)
    """
    s = Spirv()
    main_id = s.id(); out0 = s.id(); out1 = s.id(); in_col = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    p_out_v4 = s.id(); p_in_v3 = s.id(); c_one = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out0, out1, in_col])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out0, 30, 0])
    s.inst(0x0047, [out1, 30, 1])
    s.inst(0x0047, [in_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v3, 1, t_v3])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, out0, 3])
    s.inst(0x003B, [p_out_v4, out1, 3])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    lbl = s.id(); col = s.id()
    cx = s.id(); cy = s.id(); cz = s.id()
    v0 = s.id(); v1 = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x0051, [t_f32, cx, col, 0])
    s.inst(0x0051, [t_f32, cy, col, 1])
    s.inst(0x0051, [t_f32, cz, col, 2])
    s.inst(0x0050, [t_v4, v0, cx, cy, cz, c_one])
    s.inst(0x0050, [t_v4, v1, cz, cy, cx, c_one])
    s.inst(0x003E, [out0, v0])
    s.inst(0x003E, [out1, v1])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_tex2x2():
    """FS: 采样一张 2D 纹理
    in_col(vec3 顶点色当 uv) -> uv = col.xy -> texel -> out_col
    """
    s = Spirv()
    main_id = s.id(); out_col = s.id(); in_col = s.id(); tex_var = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    t_v2 = s.id(); t_img = s.id(); t_samp = s.id(); t_simg = s.id()
    p_out = s.id(); p_in = s.id(); p_simg = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col, tex_var])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out_col, 30, 0])
    s.inst(0x0047, [in_col, 30, 0])
    s.inst(0x0047, [tex_var, 34, 0])      # DescriptorSet 0
    s.inst(0x0047, [tex_var, 33, 0])      # Binding 0

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0017, [t_v2, t_f32, 2])
    # OpTypeImage: sampled type, Dim=1(2D), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=0
    s.inst(0x0019, [t_img, t_f32, 1, 0, 0, 0, 1, 0])
    s.inst(0x001A, [t_samp])
    s.inst(0x001B, [t_simg, t_img])
    s.inst(0x0020, [p_out, 3, t_v4])
    s.inst(0x0020, [p_in, 1, t_v3])
    s.inst(0x0020, [p_simg, 0, t_simg])
    s.inst(0x003B, [p_out, out_col, 3])
    s.inst(0x003B, [p_in, in_col, 1])
    s.inst(0x003B, [p_simg, tex_var, 0])

    lbl = s.id(); col = s.id(); uv = s.id()
    si = s.id(); texel = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x004F, [t_v2, uv, col, col, 0, 1])         # OpVectorShuffle -> xy
    s.inst(0x003D, [t_simg, si, tex_var])              # OpLoad sampled image
    s.inst(0x0057, [t_v4, texel, si, uv])              # OpImageSampleImplicitLod
    s.inst(0x003E, [out_col, texel])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_tex2():
    """FS: 同时采样两张纹理（binding 0/1），按 uv.x<0.5 选边
    左半屏 = binding0 的 texel，右半屏 = binding1 的 texel
    用来验证多单元状态块（unit = binding 号）是否各自独立。
    """
    s = Spirv()
    main_id = s.id(); out_col = s.id(); in_col = s.id()
    tex_a = s.id(); tex_b = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    t_v2 = s.id(); t_img = s.id(); t_samp = s.id(); t_simg = s.id(); t_bool = s.id()
    p_out = s.id(); p_in = s.id(); p_simg = s.id(); c_half = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col, tex_a, tex_b])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out_col, 30, 0])
    s.inst(0x0047, [in_col, 30, 0])
    s.inst(0x0047, [tex_a, 34, 0])        # DescriptorSet 0
    s.inst(0x0047, [tex_a, 33, 0])        # Binding 0
    s.inst(0x0047, [tex_b, 34, 0])
    s.inst(0x0047, [tex_b, 33, 2])        # Binding 2（故意留空 binding 1）

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0014, [t_bool])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0017, [t_v2, t_f32, 2])
    s.inst(0x0019, [t_img, t_f32, 1, 0, 0, 0, 1, 0])
    s.inst(0x001A, [t_samp])
    s.inst(0x001B, [t_simg, t_img])
    s.inst(0x002B, [t_f32, c_half, 0x3F000000])   # OpConstant 0.5f
    s.inst(0x0020, [p_out, 3, t_v4])
    s.inst(0x0020, [p_in, 1, t_v3])
    s.inst(0x0020, [p_simg, 0, t_simg])
    s.inst(0x003B, [p_out, out_col, 3])
    s.inst(0x003B, [p_in, in_col, 1])
    s.inst(0x003B, [p_simg, tex_a, 0])
    s.inst(0x003B, [p_simg, tex_b, 0])

    lbl = s.id(); col = s.id(); uv = s.id(); ux = s.id()
    si_a = s.id(); si_b = s.id(); ta = s.id(); tb = s.id()
    cmp = s.id(); res = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x004F, [t_v2, uv, col, col, 0, 1])
    s.inst(0x0051, [t_f32, ux, uv, 0])             # OpCompositeExtract uv.x
    s.inst(0x003D, [t_simg, si_a, tex_a])
    s.inst(0x003D, [t_simg, si_b, tex_b])
    s.inst(0x0057, [t_v4, ta, si_a, uv])           # 采样 unit 0
    s.inst(0x0057, [t_v4, tb, si_b, uv])           # 采样 unit 1
    s.inst(0x00B8, [t_bool, cmp, ux, c_half])      # ux < 0.5
    s.inst(0x00A9, [t_v4, res, cmp, ta, tb])       # OpSelect
    s.inst(0x003E, [out_col, res])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs_lod(lod_bits):
    """FS: 采样一张 mip 链纹理，使用显式 LOD
    OpImageSampleExplicitLod(0x0058) + ImageOperands mask=0x2 (Lod)。
    """
    s = Spirv()
    main_id = s.id(); out_col = s.id(); in_col = s.id(); tex_var = s.id()
    t_void = s.id(); t_fn = s.id(); t_f32 = s.id(); t_v3 = s.id(); t_v4 = s.id()
    t_v2 = s.id(); t_img = s.id(); t_samp = s.id(); t_simg = s.id()
    p_out = s.id(); p_in = s.id(); p_simg = s.id(); c_lod = s.id()

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst(0x000F, [4, main_id, b"main", out_col, in_col, tex_var])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out_col, 30, 0])
    s.inst(0x0047, [in_col, 30, 0])
    s.inst(0x0047, [tex_var, 34, 0])      # DescriptorSet 0
    s.inst(0x0047, [tex_var, 33, 0])      # Binding 0

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0017, [t_v2, t_f32, 2])
    s.inst(0x0019, [t_img, t_f32, 1, 0, 0, 0, 1, 0])
    s.inst(0x001A, [t_samp])
    s.inst(0x001B, [t_simg, t_img])
    s.inst(0x002B, [t_f32, c_lod, lod_bits])    # OpConstant LOD
    s.inst(0x0020, [p_out, 3, t_v4])
    s.inst(0x0020, [p_in, 1, t_v3])
    s.inst(0x0020, [p_simg, 0, t_simg])
    s.inst(0x003B, [p_out, out_col, 3])
    s.inst(0x003B, [p_in, in_col, 1])
    s.inst(0x003B, [p_simg, tex_var, 0])

    lbl = s.id(); col = s.id(); uv = s.id()
    si = s.id(); texel = s.id()
    s.inst(0x0036, [t_void, main_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x004F, [t_v2, uv, col, col, 0, 1])
    s.inst(0x003D, [t_simg, si, tex_var])
    # OpImageSampleExplicitLod: ... coord, mask=Lod(0x2), lod
    s.inst(0x0058, [t_v4, texel, si, uv, 0x2, c_lod])
    s.inst(0x003E, [out_col, texel])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


LOD_BITS = {0: 0x00000000, 1: 0x3F800000, 2: 0x40000000, 3: 0x40400000}


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
    'alpha_half': (build_vs_base, build_fs_alpha_half),
    'mrt2': (build_vs_base, build_fs_mrt2),
    'tex2x2': (build_vs_base, build_fs_tex2x2),
    'tex2': (build_vs_base, build_fs_tex2),
}

if __name__ == '__main__':
    mode = sys.argv[1]
    out = sys.argv[2]
    if mode == 'lod':
        txt = ("/* spirv_lod.h - gen_spirv_exp.py mode=lod */\n#include <stdint.h>\n\n"
               + emit_c("vs_spv", build_vs_base()) + "\n\n")
        for lvl, bits in sorted(LOD_BITS.items()):
            txt += emit_c("fs_spv_lod%d" % lvl, build_fs_lod(bits)) + "\n\n"
        with open(out, "w", newline="\n") as f:
            f.write(txt)
        print("wrote %s (mode=lod, 4 个 LOD 变体)" % out)
        sys.exit(0)
    vsf, fsf = MODES[mode]
    txt = ("/* spirv_tri.h - gen_spirv_exp.py mode=%s */\n#include <stdint.h>\n\n" % mode
           + emit_c("vs_spv", vsf()) + "\n\n" + emit_c("fs_spv", fsf()) + "\n")
    with open(out, "w", newline="\n") as f:
        f.write(txt)
    print("wrote %s (mode=%s)" % (out, mode))
