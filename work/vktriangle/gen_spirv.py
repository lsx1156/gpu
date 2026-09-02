#!/usr/bin/env python3
"""
Generate EXACT c792f37 multicolor SPIR-V with CORRECT 2-word inline string for "main".
"""

import struct

MAGIC = 0x07230203
VERSION = 0x00010000

class Spirv:
    def __init__(self):
        self.words = []
        self.next_id = 1
    
    def id(self):
        i = self.next_id
        self.next_id += 1
        return i
    
    def _encode_string(self, s):
        b = s.encode('ascii') + b'\0'
        pad = (4 - len(b) % 4) % 4
        b += b'\0' * pad
        words = []
        for k in range(0, len(b), 4):
            words.append(struct.unpack('<I', b[k:k+4])[0])
        return words
    
    def _track(self, op):
        if isinstance(op, int) and 0 < op < 0x100000:
            self.next_id = max(self.next_id, op + 1)
    
    def inst(self, opcode, operands):
        body = []
        for op in operands:
            body.append(op)
        
        if opcode == 0x000F:
            if len(operands) >= 2: self._track(operands[1])
            for op in operands[3:]: self._track(op)
        elif opcode in (0x0016, 0x0017, 0x0018, 0x0019, 0x001A, 0x001B,
                        0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A):
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
        elif opcode in (0x002B, 0x002C, 0x002D, 0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035):
            if len(operands) >= 2:
                self._track(operands[0])
                self._track(operands[1])
        elif opcode == 0x0036:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
            if len(operands) >= 4: self._track(operands[3])
        elif opcode == 0x003B:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
        elif opcode == 0x003D:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
            if len(operands) >= 3: self._track(operands[2])
        elif opcode == 0x003E:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
        elif opcode == 0x0047:
            if len(operands) >= 1: self._track(operands[0])
        elif opcode == 0x0050:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
            for op in operands[2:]: self._track(op)
        elif opcode == 0x0059:
            if len(operands) >= 1: self._track(operands[0])
            if len(operands) >= 2: self._track(operands[1])
            if len(operands) >= 3: self._track(operands[2])
        elif opcode == 0x00F8:
            if len(operands) >= 1: self._track(operands[0])
        
        self.words.append(((len(body) + 1) << 16) | opcode)
        self.words += body
    
    def inst_entry_point(self, exec_model, entry_point_id, name, interface_ids):
        str_words = self._encode_string(name)
        body = [exec_model, entry_point_id] + str_words + interface_ids
        self.words.append(((len(body) + 1) << 16) | 0x000F)
        self.words += body
        self._track(entry_point_id)
        for op in interface_ids:
            self._track(op)
    
    def header(self):
        return [MAGIC, VERSION, 0, 0x70000000, 0]


def build_vs():
    s = Spirv()
    main_id = s.id()       # 1
    gl_pos = s.id()        # 2
    out_col = s.id()       # 3
    in_pos = s.id()        # 4
    in_col = s.id()        # 5
    
    t_void = s.id()        # 6
    t_fn = s.id()          # 7
    t_f32 = s.id()         # 8
    t_v2 = s.id()          # 9
    t_v3 = s.id()          # 10
    t_v4 = s.id()          # 11
    p_out_v4 = s.id()      # 12
    p_out_v2 = s.id()      # 13
    p_in_v3 = s.id()       # 14
    c_one = s.id()         # 15

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst_entry_point(0, main_id, "main", [gl_pos, out_col, in_pos, in_col])
    s.inst(0x0003, [1, 450])
    s.inst(0x0047, [gl_pos, 11, 0])
    s.inst(0x0047, [in_pos, 30, 0])
    s.inst(0x0047, [in_col, 30, 1])
    s.inst(0x0047, [out_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v2, t_f32, 2])
    s.inst(0x0017, [t_v3, t_f32, 3])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_out_v2, 3, t_v2])
    s.inst(0x0020, [p_in_v3, 1, t_v3])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, gl_pos, 3])
    s.inst(0x003B, [p_out_v2, out_col, 3])
    s.inst(0x003B, [p_in_v3, in_pos, 1])
    s.inst(0x003B, [p_in_v3, in_col, 1])

    fn_id = main_id
    lbl = s.id()       # 16
    pos = s.id()       # 17
    col = s.id()       # 18
    glp = s.id()       # 19
    x = s.id()         # 20
    y = s.id()         # 21
    xy = s.id()        # 22
    
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v3, pos, in_pos])
    s.inst(0x003D, [t_v3, col, in_col])
    s.inst(0x0059, [t_f32, x, col, 0])
    s.inst(0x0059, [t_f32, y, col, 1])
    s.inst(0x0050, [t_v2, xy, x, y])
    s.inst(0x003E, [out_col, xy])
    s.inst(0x003D, [t_v3, pos, in_pos])
    s.inst(0x0050, [t_v4, glp, pos, c_one])
    s.inst(0x003E, [gl_pos, glp])
    s.inst(0x00FD, [])
    s.inst(0x0038, [])
    return s


def build_fs():
    s = Spirv()
    main_id = s.id()      # 1
    out_col = s.id()      # 2
    in_col = s.id()       # 3
    
    t_void = s.id()       # 4
    t_fn = s.id()         # 5
    t_f32 = s.id()        # 6
    t_v2 = s.id()         # 7
    t_v4 = s.id()         # 8
    p_out_v4 = s.id()     # 9
    p_in_v2 = s.id()      # 10
    c_zero = s.id()       # 11
    c_one = s.id()        # 12

    s.inst(0x0011, [1])
    s.inst(0x000E, [0, 1])
    s.inst_entry_point(4, main_id, "main", [out_col, in_col])
    s.inst(0x0010, [main_id, 7])
    s.inst(0x0047, [out_col, 30, 0])
    s.inst(0x0047, [in_col, 30, 0])

    s.inst(0x0013, [t_void])
    s.inst(0x0021, [t_fn, t_void])
    s.inst(0x0016, [t_f32, 32])
    s.inst(0x0017, [t_v2, t_f32, 2])
    s.inst(0x0017, [t_v4, t_f32, 4])
    s.inst(0x0020, [p_out_v4, 3, t_v4])
    s.inst(0x0020, [p_in_v2, 1, t_v2])
    s.inst(0x002B, [t_f32, c_zero, 0x00000000])
    s.inst(0x002B, [t_f32, c_one, 0x3F800000])

    s.inst(0x003B, [p_out_v4, out_col, 3])
    s.inst(0x003B, [p_in_v2, in_col, 1])

    fn_id = main_id
    lbl = s.id()       # 13
    col = s.id()       # 14
    cv = s.id()        # 15
    s.inst(0x0036, [t_void, fn_id, 0, t_fn])
    s.inst(0x00F8, [lbl])
    s.inst(0x003D, [t_v2, col, in_col])
    s.inst(0x0050, [t_v4, cv, col, c_zero, c_one])
    s.inst(0x003E, [out_col, cv])
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


hdr = """/* spirv_tri.h - c792f37 multicolor with CORRECT string encoding */
#include <stdint.h>

"""
vs = build_vs()
fs = build_fs()
out = hdr + emit_c("vs_spv", vs) + "\n" + emit_c("fs_spv", fs) + "\n"
with open("D:/project/gpu/work/vktriangle/spirv_tri.h", "w", newline="\n") as f:
    f.write(out)

print(f"VS bound: {vs.header()[3]}, words: {len(vs.header() + vs.words)}")
print(f"FS bound: {fs.header()[3]}, words: {len(fs.header() + fs.words)}")
print("spirv_tri.h written")