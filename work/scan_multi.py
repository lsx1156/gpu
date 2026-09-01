#!/usr/bin/env python3
"""扫描 multicolor rd 的所有 CS 链，打印每个链的 w+pkt 摘要与 VPC/SP/HLSQ 寄存器。"""
import struct, os
import xml.etree.ElementTree as ET
base = r"D:\project\gpu\work"
xmlp = os.path.join(base,"mesa-src","mesa-24.0.5","src","freedreno","registers","adreno","a5xx.xml")
tree = ET.parse(xmlp); root = tree.getroot()
for el in root.iter(): el.tag = el.tag.split("}")[-1]
regs = {}
for r in root.iter("reg32"):
    o,n = r.get("offset"), r.get("name")
    if o and n: regs.setdefault(int(o,16),[]).append(n)
arrays = []
for a in root.iter("array"):
    o,s,n,l = a.get("offset"),a.get("stride"),a.get("name"),a.get("length")
    if o and n and s and l: arrays.append((int(o,16),int(s,16),int(l,16),n))
arrays.sort(key=lambda t:-t[0])
def regname(v):
    for b0,s,l,n in arrays:
        if b0<=v<b0+s*l:
            return f"{n}[{(v-b0)//s}]" if (v-b0)%s==0 else f"{n}[{(v-b0)//s}]+{(v-b0)%s}"
    if v in regs: return regs[v][0]
    for b0 in sorted(regs, reverse=True):
        if b0<v: return f"{regs[b0][0]}+0x{v-b0:x}"
    return f"0x{v:x}"
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
            for k,v in enumerate(ws[i+1:i+1+c]): out.append(("w",reg+k,v))
            i+=1+c
        elif t==7:
            op=(w>>16)&0x7F; c=w&0x7FFF; body=ws[i+1:i+1+c]
            if op==0x3f and c>=3 and depth<3:
                tgt=body[0]|(body[1]<<32); sz=body[2]&0xFFFF
                walk(bos,tgt,sz,out,depth+1)
            i+=1+c
        else: i+=1
import sys
port=sys.argv[1] if len(sys.argv)>1 else os.path.join(base,"vktriangle","multicolor_00001.rd")
bos,cs=parse_rd(port)
print(f"{os.path.basename(port)}: {len(cs)} CS chains")
for ci,(iova,sz) in enumerate(cs):
    out=[]; walk(bos,iova,sz,out)
    nw=sum(1 for e in out if e[0]=="w")
    print(f"--- chain {ci} iova=0x{iova:x} sz={sz} writes={nw} ---")
    last={}
    for e in out:
        if e[0]=="w":
            nm=regname(e[1])
            if any(k in nm for k in ("VPC","SP_FS","SP_VS","HLSQ","GRAS","VFD_D","SP_PRIM","PC_PRIM")) or nm=="PC_UNKNOWN":
                if nm not in last or 1:
                    last[nm]=e[2]
                    print(f"  0x{e[1]:05x} {nm:<42} = 0x{e[2]:08x}")