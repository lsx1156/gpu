#!/usr/bin/env python3
"""Android boot image v0 打包/解包/校验工具（mido / lk2nd 用）
用法:
  python bootimg.py unpack <img> <outdir>      # 解包 + 打印头参数 + 保存 params.json
  python bootimg.py pack <params.json> <outdir> <out.img>  # 按参数打包
  python bootimg.py verify <img1> <img2>       # 有效区字节级比对
"""
import json, os, struct, sys

V0_FMT = '<8s10I16s512s32s1024s'   # magic, 10 fields, name, cmdline, id, extra_cmdline
F = ['kernel_size','kernel_addr','ramdisk_size','ramdisk_addr','second_size',
     'second_addr','tags_addr','page_size','dt_size','unused']

def parse(img_path):
    d = open(img_path,'rb').read()
    vals = struct.unpack_from(V0_FMT, d, 0)
    magic = vals[0]
    assert magic == b'ANDROID!', f'bad magic {magic!r}'
    h = dict(zip(F, vals[1:11]))
    h['name'] = vals[11].rstrip(b'\0').decode()
    h['cmdline'] = vals[12].rstrip(b'\0').decode()
    h['id'] = vals[13].hex()
    h['extra_cmdline'] = vals[14].rstrip(b'\0').decode()
    return h, d

def align(n, ps): return (n + ps - 1) // ps * ps

def unpack(img_path, outdir):
    h, d = parse(img_path)
    ps = h['page_size']
    n = 1  # header pages
    os.makedirs(outdir, exist_ok=True)
    for key, fname in [('kernel_size','kernel'),('ramdisk_size','ramdisk'),('second_size','second'),('dt_size','dt')]:
        if h[key]:
            blob = d[n*ps : n*ps + h[key]]
            open(os.path.join(outdir, fname),'wb').write(blob)
        n += align(h[key], ps) // ps if h[key] else 0
    # header/布局信息存档
    h['img_file_size'] = len(d)
    h['valid_size'] = n * ps
    json.dump(h, open(os.path.join(outdir,'params.json'),'w'), indent=2)
    print(json.dumps(h, indent=2))
    print(f'valid region: {n*ps} bytes (file {len(d)})')

def pack(params_path, srcdir, out_path):
    h = json.load(open(params_path))
    ps = h['page_size']
    out = bytearray(align(1632, ps))
    struct.pack_into(V0_FMT, out, 0,
        b'ANDROID!',
        *[h[k] for k in F],
        h['name'].encode().ljust(16, b'\0'),
        h['cmdline'].encode().ljust(512, b'\0'),
        bytes.fromhex(h['id']),
        h['extra_cmdline'].encode().ljust(1024, b'\0'))
    img = build_image(h, srcdir, ps, out)
    open(out_path,'wb').write(img)
    print(f'packed -> {out_path} ({len(img)} bytes)')

def build_image(h, srcdir, ps, out):
    # id 字段：params 里非空则原样保留（复现原文件）；为空则按内容重算 sha1（新打包）
    blobs = []
    for key, fname in [('kernel_size','kernel'),('ramdisk_size','ramdisk'),('second_size','second'),('dt_size','dt')]:
        p = os.path.join(srcdir, fname)
        if h[key] and os.path.exists(p):
            b = open(p,'rb').read()
            assert len(b) == h[key], f'{fname}: {len(b)} != {h[key]}'
            blobs.append(b)
    if not h['id'].strip('0'):
        import hashlib
        sha = hashlib.sha1()
        for b in blobs: sha.update(b)
        struct.pack_into('<32s', out, 8 + 16*4 + 16 + 512, sha.digest())
    off = align(1632, ps)
    img = bytearray(off)
    img[:len(out)] = out
    pos = ps
    for key, b in zip(['kernel_size','ramdisk_size','second_size','dt_size'], blobs):
        end = pos + len(b)
        if end > len(img): img += bytearray(end - len(img))  # 扩展到写入末尾
        img[pos:end] = b
        pos += align(h[key], ps)
    return img

def verify(a, b):
    da, db = open(a,'rb').read(), open(b,'rb').read()
    n = min(len(da), len(db))
    same = da[:n] == db[:n]
    tail_zero = all(x == 0 for x in (da[n:] if len(da)>len(db) else db[n:]))
    print(f'first {n} bytes equal: {same}; longer-tail all-zero: {tail_zero}')
    if not same:
        for i,(x,y) in enumerate(zip(da,db)):
            if x != y: print(f'first diff @ 0x{i:x}: {x:02x} vs {y:02x}'); break

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'unpack': unpack(sys.argv[2], sys.argv[3])
    elif cmd == 'pack': pack(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == 'verify': verify(sys.argv[2], sys.argv[3])
    else: print(__doc__)
