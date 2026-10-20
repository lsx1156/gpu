# M3.2 第一步：a5xx sysmem 附件清除（RB blit 引擎）

结案日期：2026-10-20 · 回归 13/13 · 探针 `vk_clear`

## 结论

a5xx 的 renderpass `loadOp = CLEAR` 在此之前**根本没有实现**：`tu_emit_subpass_begin_sysmem<A5XX>`
是空函数（注释称「待对照 fd5 补齐」）。既有的 12 个探针都用黑色清屏 + 新分配内存，
恰好与「什么都不做」等价，所以一直没暴露。

现在颜色附件的 sysmem clear 走 a5xx 的 **RB blit 引擎**，与 fd5 的 `fd5_clear()`
（`src/gallium/drivers/freedreno/a5xx/fd5_draw.c:238-300`）逐条对齐：

```
tu6_emit_mrt<A5XX>() + tu6_emit_zs<A5XX>()      # clear 写的是「当前绑定的目标」，地址必须先就位
RB_RENDER_CNTL   = 0x0                          # fd5_emit_render_cntl(blit=true)：清 0x8
GRAS_SC_CNTL     = 0x8
RB_RESOLVE_CNTL_1..2 = render area 的 (x1,y1)/(x2,y2)
每颜色附件：
  RB_BLIT_CNTL      = BUF(BLIT_MRT0 + i)
  RB_CLEAR_CNTL     = FAST_CLEAR | MASK(0xf)
  RB_CLEAR_COLOR_DW0..3 = util_pack_color_union(fmt, 按 MRT 的 SWAP 重排后的分量)
  CP_EVENT_WRITE(4 dw) = BLIT + 时间戳地址 + 0
恢复 RB_RENDER_CNTL = 0x8、GRAS_SC_CNTL = 0x8，然后 WFI
```

出处：`fd5_emit.h:162-184`（render_cntl 的 blit 分支）、`fd5_draw.c:246`（`fd5_emit_render_cntl(ctx, true, false)`）、
`fd5_draw.c:258-286`（swap + `util_pack_color_union`）、`fd5_draw.c:288-296`（BLIT_CNTL/CLEAR_CNTL）、
`fd5_emit.h:141-151` + `fd5_emit.h:154-158`（`CP_EVENT_WRITE` 带 timestamp，共 4 个 dword）,
`src/freedreno/registers/adreno/a5xx.xml:242-252`（`BLIT_MRT0..MRT7 = 0..7`）。

## 两个踩到的坑

1. **`CP_EVENT_WRITE(BLIT)` 必须发 4 个 dword。** fd5 的 `fd5_emit_blit()` 走
   `fd5_event_write(..., BLIT, timestamp = true)`，包长 4（event + 时间戳地址 LO/HI + 0）。
   先按 1 个 dword 发，硬件把后续 dword 当时间戳地址写入 → 首次 `vkQueueWaitIdle` 返回
   **-4 (VK_ERROR_DEVICE_LOST)**，一行像素都没写。改用 `tu_get_scratch_bo(cmd->device, 16, &bo)`
   的 IOVA 作时间戳落点后正常。
2. **blit 模式的 `RB_RENDER_CNTL` 要清 0x8。** 此前移植 `fd5_emit_render_cntl` 只搬了
   「非 binning、非 blit」分支（两个寄存器都是 0x8）。blit 时 `RB_RENDER_CNTL = 0`，
   `GRAS_SC_CNTL` 仍是 0x8；clear 前后都要成对切换，否则后续 draw 的 ROP 行为异常。

## 判据（`work/vktriangle/vkclear.c`，64×64 R8G8B8A8_UNORM 线性 RT，host-visible 回读）

- pass1：整幅 clear 到 (0.2,0.4,0.6,1.0) → `期望(51,102,153,255)=4096 其它=0 总=4096`，
  首像素 `ff996633`（小端字节序 R=0x33,G=0x66,B=0x99,A=0xff）。
- pass2：`renderArea = (16,16)+(16,16)` 清成红色 → `内 256 (红 256 / 其它 0) 外 3840`，
  边界像素 `(16,16)=ff0000ff (15,15)=ff996633 (32,32)=ff996633` → RESOLVE_CNTL 窗口精确到像素。

`vk_clear` 已入 `tools/tu5xx_expect.json`（第 13 例，4 项 `contains`）；`--mutate --case vk_clear`
返回 rc=1，证明判据有效。全量回归 `work/out/regress_13.log`：13 通过 / 0 失败。

## 仍未做

- 深度/模板附件的 clear（一次性 `logw` 告警）；探针均无深度附件。
- GMEM 渲染路径：`tu6_tile_render_begin<A5XX>` 仍是 a6xx 代码，`TU_DEBUG=gmem` 下
  `vktri_base` 输出全黑（rc=4），与「gmem 未实现」一致。M3.2 的后续步骤从这里继续。
