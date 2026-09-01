# H2b 决定性对比：WORKING fd5 GL vs BROKEN tu5xx 的 FS varying 递送 (2026-09-01)

## 目标
FS 读 color varying 全黑(nonbg=0)，锁定 tu(LS) varying 递送缺项。

## 方法（用户选定）
IR3_SHADER_DEBUG=disasm 对比：工作 GL smooth-varying 参照(glsmooth.c, X11, center=128,0,128)
  vs 坏 tu5xx multicolor(vktriangle, nonbg=0)。

## WORKING fd5 GL smooth（glsmooth_disasm.log, FRAG prog 4/1）
- VS out:  r0.x(VARYING_SLOT_POS) r1.z(VARYING_SLOT_VAR0 slot=32)   // 单 varying，经 bary.f
- FS in:   r0.z(VARYING_SLOT_VAR0 slot=32 cm=3,il=0,b=1)
           r0.x(SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL slot=53 cm=3,il=2,b=0)
- FS body: bary.f r0.z, 0, r0.x      <- 硬件重心插值，FS 无需 l[] 基址
           bary.f (ei)r1.x, 1, r0.x
           mov.u32u32 r0.w, 0
           mov.u32u32 r1.y, 0x3f800000
- FS out:  r0.z(FRAG_RESULT_COLOR)

## BROKEN tu5xx multicolor（tu_now.log, 当前部署, readback nonbg=0）
- VS out:  r0.x(VARYING_SLOT_POS) r1.x(VARYING_SLOT_VAR1 slot=33 cm=7)  // passthru 3 comps
- FS in:   r0.x(VARYING_SLOT_VAR1 slot=33 cm=7,il=0,b=1)
           r63.x(SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL slot=53 cm=3,il=3,b=0)
- FS body: ldlv.u32 r0.x, l[0], 1
           ldlv.u32 r0.y, l[1], 1
           ldlv.u32 r0.z, l[2], 1
           (ss)bary.f (ei)r63.x, 0, r0.x
- FS out:  r0.x(FRAG_RESULT_DATA0)

## 关键差异
1. GL 用 bary.f(硬件重心) 读 varying -> WORK。
2. tu 用 ldlv 从 l[0..2] 直读 varying 存储 -> 读回 0（FLAT 实验态）。
3. 历史：tu 平滑 bary.f 变体(已试)也读 0 -> 排除"只看 ij/bary"假设。
4. -> 根因收敛：**VPC/SP 配置没把 VS 颜色写进 FS 可读的 per-primitive varying 存储**
   （ldlv 连原始 per-vertex 值都是 0，bary 也 0，双侧证 varyings 根本没送达 FS）。
   GL 同一几何可用 -> 差异在 tu 内部 VPC_PACK / SP_VS_OUT / FS local-base 递送，
   H2a 曾证"寄存器存在且值'正确'"但那是旧 fd5_kernel.rd(glmark 多 varying)对照，
   未必覆盖单 VAR1 slot=33 布局与 tu 自身 FS ldlv 基址(l[0]/SP local base)。

## 下一步候选(按序)
A. 核对 tu 发射的 SP_VS_OUT[0]/SP_VS_VPC_DST[0]/SP_FS_CTRL_REG0/VPC_PACK 对"这一个"
   VAR1->slot33->FS var1 bank"布局的寄存器值，与 GL 同几何(把 glsmooth 扩成 3 分量
   颜色做同布局)重抓 FD_MESA 的 VPC 值对比 -> 找布局不一致。
B. 若寄存器又全一致：查看 tu5xx 是否设了 FS 读 varying 的 local/IBO 基址
   (SP_IBO_BASE / 每 draw 的 varying buffer base / PC_PRIMITIVE_CNTL STRIDE)，
   对照 a5xx 用的 GL(glsmooth 的 FD_MESA 命令流) 的 FS 输入 varying base。
C. 让 tu 走 bary.f 硬件插值(与 GL 一致)替代 ldlv 直读。

## 文件
- work/glsmooth.c, work/run_glsmooth_disasm.sh -> glsmooth_disasm.log (GL 参照)
- work/vktriangle/run_fs_flat_disasm.txt (tu 旧 FLAT 实验, 也读 0)
- work/tu_now.log (当前部署 tu multicolor FS/VS disasm)