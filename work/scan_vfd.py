#!/usr/bin/env python3
"""dump multicolor 全 VFD 寄存器 + SP_VS_OUT/VPC 一行, 看是否有 color 属性流。"""
import struct, os
base = r"D:\project\gpu\work"
def parse_rd(path):
    data=open(path,"rb").read(); bos,cs=[],[]
    off=0
    while off+8<=len(data):
        typ,sz=struct.unpack_from("<II",data,off); off+=8
        payload=data[off:off+sz]; off+=sz
        if typ==3:
            iova,size=struct.unpack("<II",payload[:8]); bos.append([iova,size,None])
        elif typ==12:
            for b in reversed(bos):
                if b[2] is None: b[2]=payload; break
        elif typ==6: cs.append(struct.unpack("<II",payload[:8]))
    return bos,cs
def find_bo(bos,iova): return next((b for b in bos if b[2] and b[0]<=iova<b[0]+b[1]),None)
def words_of(bos,iova,n):
    b=find_bo(bos,iova)
    if not b: return None
    o=iova-b[0]; avail=(len(b[2])-o)//4; n=min(n,avail)
    return list(struct.unpack_from(f"<{n}I",b[2],o))
def walk(bos,iova,n,out,depth=0):
    ws=words_of(bos,iova,n)
    if ws is None: return
    i=0
    while i<len(ws):
        w=ws[i]; t=(w>>28)&0xF
        if t==4:
            reg=(w>>8)&0x3FFFF; c=w&0x7F
            for k,v in enumerate(ws[i+1:i+1+c]): out.append((reg+k,v))
            i+=1+c
        elif t==7:
            op=(w>>16)&0x7F; c=w&0x7FFF; body=ws[i+1:i+1+c]
            if op==0x3f and c>=3 and depth<3:
                tgt=body[0]|(body[1]<<32); sz=body[2]&0xFFFF
                walk(bos,tgt,sz,out,depth+1)
            i+=1+c
        else: i+=1
def dump(path):
    bos,cs=parse_rd(path); best=None
    for iova,sz in cs:
        out=[]; walk(bos,iova,sz,out)
        n=sum(1 for e in out)
        if best is None or n>best[0]: best=(n,out)
    return best[1]
import sys
rd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(base,"vktriangle","multicolor_00001.rd")
stream=dump(rd)
interest=range(0x0e400,0x0e4f2)
print("=== VFD 区间 (0x0e400..0x0e4f1) last-value ===")
last={}
for r,v in stream:
    if 0x0e400<=r<=0x0e4f1: last[r]=v
for r in sorted(last):
    print(f"  0x{r:05x} = 0x{last[r]:08x}")
if "--seq" in sys.argv:
    print("=== VFD 写序 (VFD_CONTROL_0, VFD_DEST_CNTL) ===")
    for r,v in stream:
        if r in (0x0e400,) or 0x0e4c0<=r<=0x0e4cb:
            print(f"  0x{r:05x} = 0x{v:08x}")