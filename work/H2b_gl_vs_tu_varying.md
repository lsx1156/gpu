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

---

## GPU 级 trace 抓事件（2026-09-03）

### 手段（ftrace 不可用：内核未编 CONFIG_FTRACE，无 tracefs）
1. **dmesg drm.debug**：`echo 0x3 > /sys/module/drm/parameters/debug`（CORE|DRIVER）+ `hang_debug=1`。
   跑 vktriangle 后 dmesg 抓到 GPU 事件序列：MSM_GEM_SUBMIT ×2 / msm_gpu_pm_resume(5.0.6.0) /
   SYNCOBJ_WAIT / gem_new "invalid cache flag: 80000"(-22，已知旧内核 flag 差异，app 正常)。
   `work/trace_gpu_events.sh`。
2. **内核 rd 节点**（真正 GPU 级命令流）：`cat /sys/kernel/debug/dri/1a01000.display-controller/rd` 后台读 +
   跑 app，抓到内核实际提交到 ring 的 RD 流（vktriangle 2 提交 + Xorg fd5 刷屏，共 85 提交 6.5MB）。
   `work/trace_rd.sh`。注意：rd 节点单次单读，遗留 `cat` 会占住导致新 open EBUSY（杀之即可）；
   无 msm_gpu 独立 debugfs 目录，rd/perf/gpu 均在 dri 设备目录下。
3. **perf 节点**：%BUSY 历史（渲染时最高 23%）；**gpu 节点**：完整 ring/寄存器状态（ascii85）。

### 解析：work/parse_krd2.py（按 RD_CMD "comm/pid: fence=" 记录分组）
- 内核 RD 记录格式 `[type u32][size u32 字节][载荷]`；type 1=TEST 2=CMD(ascii) 3=GPUADDR{iova,size,hi}
  6=CMDSTREAM_ADDR{iova,szd,hi} 12=BUFFER_CONTENTS 13=GPU_ID 14=CHIP_ID（msm_rd.c enum）。
- 必须按 comm 过滤只取 vktriangle 提交，否则 Xorg(fd5) 提交会污染（之前 rd_key_regs 取到的是 Xorg 的）。

### vktriangle 提交的内核级寄存器（权威 GPU 值）
```
VPC_CNTL_0=0x10807        VPC_PACK=0xff03          VPC_VAR[0]=0xfffffff8
SP_VS_OUT[0]=0x0f000704   SP_VS_VPC_DST[0]=0x00000300
SP_FS_CTRL_REG0=0x50806   SP_FS_OUTPUT_CNTL=0x001f9f81
HLSQ_CONTROL_0=0x880      SP_FS_CONFIG=0x3f01      HLSQ_FS_CONFIG=0x3f01
```

### TWO_QUADS 实验结论（真实验证，排除线程大小）
- 修正：a3xx_threadsize 枚举 **TWO_QUADS=0, FOUR_QUADS=1**（adreno_common.xml:338），
  SP_FS_CTRL_REG0 THREADSIZE(pos3) 与 HLSQ_CONTROL_0 FSTHREADSIZE(pos0) 置位 = FOUR_QUADS。
- 强制 FS TWO_QUADS 重编部署后，内核 RD 证实：SP_FS_CTRL_REG0 bit3=0、HLSQ_CONTROL_0 bit0=0 → **FS 已是 TWO_QUADS**。
- 蓝分量仍 0（blue probe=RGB(156,164,0)，与改前完全一致）→ **FS 线程大小不是第3分量根因，排除**。
- 下一个证据：上述内核级 VPC/SP 值正好用于候选 A（对照 fd5 glsmooth 3 分量同布局）。

### 文件
- work/trace_rd.sh, work/trace_gpu_events.sh, work/parse_krd.py, work/parse_krd2.py
- work/krd/krd.rd（6.5MB 内核 RD 采样，未入 git，可经 trace_rd.sh 复现）