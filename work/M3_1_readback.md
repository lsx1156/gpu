# M3.1 第三段：读回方向（vkCmdCopyImageToBuffer）—— 真机通过

日期：2026-10-20　设备：红米 Note 4X（mido）/ Adreno 506 / MSM8953 / Ubuntu 24.04
驱动：tu5xx（Mesa 24.0.5 freedreno 派生，A5XX Vulkan 后端）

## 结论

`vkCmdCopyImageToBuffer` 在芯片 5 上可用，判据为像素级精确：16×16 渲染结果经
copyImageToBuffer 落到普通 buffer，四象限 64 像素 W/R/G/B 全对，总计 256/256。

```
  img 4x4 OPTIMAL mem=8192
  img 16x16 LINEAR  mem=8192
  submit 完成，等 fence
  fence 已 signal
  分布: W=64 R=64 G=64 B=64 其它=0 (总 256)
  quad0 ( 3, 3)=RGBA(255,255,255,255) 期望 W
  quad1 (12, 3)=RGBA(255,0,0,255) 期望 R
  quad2 ( 3,12)=RGBA(0,255,0,255) 期望 G
  quad3 (12,12)=RGBA(0,0,255,255) 期望 B
  DONE
RC=0
```

## 为什么不能沿用上传方向的「提交前 CPU 拷贝」

上传（buffer→image）在 KNL 提交**之前**拷，语义等价于「GPU 开跑前数据已就位」。
读回方向不能这么做：

1. 提交前拷 → 拿不到本提交里的渲染结果；
2. 提交后立刻在 `driver_submit` 里拷 → 不等 GPU 完成，读到旧内容；而在
   `driver_submit` 里等空闲会**死锁**（`vk_queue.c` 的 `vk_queue_submit_final`
   在持队列 `submit.mutex` 的状态下才调用 `driver_submit`）。

## 实现（三条）

1. **节点在提交后搬到设备级待办链**：`tu_queue_submit()` 里 `chip == 5` 时调
   `tu5xx_cpu_copy_detach_readbacks(cmd, dev)`，把 `to_buffer` 节点从
   `cmd->cpu_copies` 摘下来挂到 `dev->pending_readbacks`。此处不等 GPU。
2. **在 `vkMapMemory` 里补拷**：`tu_MapMemory` 开头调 `tu5xx_cpu_readback_drain(dev)`——
   先对每个队列 `vk_common_QueueWaitIdle`，再执行所有挂起读回。此时不持队列锁，
   等待安全；应用本来就「等 fence → map」，时序天然正确。
3. **设备销毁时释放未消费节点**：`tu_DestroyDevice` 调 `tu5xx_cpu_readback_free_all(dev)`。

拷贝实体仍是 `tu5xx_cpu_copy_run_one()`：按 `fdl_layout` 的 `slices[level].offset` +
`layer_size` + `fdl_pitch` 逐行 memcpy，方向由 `node->to_buffer` 决定；
`bufferRowLength` / `bufferImageHeight` / `imageOffset` / mip / 数组层都支持；
仅 `tile_mode != 0` 时 logw 一次并跳过（TILE5_3 swizzle 掩码随宽度变，无闭式）。

## 语义偏差（已显式告警，不静默）

CPU 读回只能反映**提交结束时**的图像内容。若同一命令缓冲里在 `vkCmdCopyImageToBuffer`
之后又写过同一图像，内容可能与「命令流中此刻」不同。为此加了 `cmd->img_writes`
（`(img, epoch)` 动态数组）与 `tu5xx_note_image_write()` 钩子，覆盖：

- render pass / dynamic rendering 的附件（挂在 `tu_choose_gmem_layout()` 的附件循环里）
- `vkCmdClearColorImage` / `vkCmdClearDepthStencilImage` / `vkCmdClearAttachments`
- `vkCmdCopyImage` / `vkCmdBlitImage` / `vkCmdResolveImage` 的目标
- `vkCmdCopyBufferToImage` 的目标（复用上传路径的注册点）

命中冲突时 `logw` 一次：“读回内容为提交结束时的状态（5xx CPU 读回的限制）”。

## 已知未覆盖

- `vkCmdCopyImage` / `vkCmdBlitImage` / `vkCmdResolveImage` 的 **GPU 侧执行**仍走 r2d，
  5xx 上无 `A5XX_RB_2D_*`，必 DEVICE_LOST。目前它们只被用作「写入记录」，功能未实现。
- 持久映射（map 一次、后续不再 map）后直接读 GPU 写入的 buffer，不会触发 drain。

## 验证

| 项 | 结果 |
|---|---|
| `vk_down` 真机 | 256/256 像素正确，rc=0（缓冲区预填 0xCD，非残留数据） |
| 回归套件 | 12/12 PASS（新增 `vk_down`，`contains` 7 项） |
| `--mutate --case vk_down` | rc=1，报 `0 通过 / 1 失败`（判据有效） |
| 增量构建 | `incremental build exit=0` |

探针：`work/vktriangle/vkdown.c`（364 行，由 `vkup.c` 派生，末尾追加
`vkCmdCopyImageToBuffer` → fence → `vkMapMemory` 校验）。
编译：`gcc -O1 -o vk_down vkdown.c -I/home/umeko/tu5xx -I. -ldl`
运行：`VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json TU_DEBUG=sysmem MESA_SHADER_CACHE_DISABLE=true ./vk_down`
