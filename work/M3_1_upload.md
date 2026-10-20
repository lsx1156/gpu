# M3.1 第二段：主机侧上传路径（vkCmdCopyBufferToImage）

状态：**上传方向真机通过**（2026-10-20）。读回方向仍未实现，见末尾“遗留”。

## 1. 结论（先看这段）

1. **a5xx 没有 2D blitter 寄存器**：源码里只有 `A6XX_RB_2D_DST_INFO` / `A6XX_RB_2D_SRC_INFO`，
   没有 `A5XX_RB_2D_*`。原 `tu_copy_buffer_to_image<CHIP=5>` 走 r2d 路径 → 写无效寄存器，
   实测 `vkQueueWaitIdle` 返回 `0xfffffffc`（VK_ERROR_DEVICE_LOST）。
2. 因此 **`vkCmdCopyBufferToImage` 在 5xx 上必须自己实现**。本机 sysmem + 全部 BO
   host-visible，选择 **CPU 逐行复制、在队列提交前执行**（`tu_queue_submit` 钩子），
   语义等价于“GPU 开跑前数据已就位”。
3. 同时把 **`VK_IMAGE_TILING_OPTIMAL` 默认按 linear 分配**（`TU5XX_TILE=1` 才启用
   `TILE5_3`）。理由：sysmem 下 tiled 无任何收益，而 tiled 目标需要像素内 swizzle 才能
   主机侧直写；默认 linear 让真实应用的纹理上传零成本可用。

## 2. TILE5_3 像素布局（实测，存档备查）

用“坐标指纹纹理 → GPU 渲染进 tiled 图像 → dump 内存 → 主机侧解指纹”反推
（`work/vktriangle/vktilemap.c`，支持 `W H` 参数）。全部尺寸下 4×4 微块内部排列一致：

```
q(x,y) = ((x>>1)&1)*4 | ((y>>1)&1)*8 | (x&1) | ((y&1)<<1)      # 0..15 word
```

微块网格（bx = x>>2, by = y>>2）为 GF(2) 线性（XOR 位掩码），**掩码只取决于宽度（pitch）**，
与高度无关（512×128 与 512×512 的掩码完全一致，128×512 与 128×128 一致）：

| 宽 | pitch | A: bx0..bx6 |
|----|-------|-------------|
| 128 | 512 | 0x1, 0x2, 0x1c, 0x28, 0x40 |
| 256 | 1024 | 0x1, 0x2, 0x1c, 0xa8, 0x40, 0x80 |
| 512 | 2048 | 0x1, 0x2, 0x1c, 0xa8, 0x40, 0x80, 0x100 |

| 宽 | B: by0..by6 |
|----|-------------|
| 128 | 0x18, 0x0c, 0x80, 0x100, 0x200, 0x400, 0x800 |
| 256 | 0x18, 0x8c, 0x100, 0x200, 0x400, 0x800 |
| 512 | 0x18, 0x8c, 0x300, 0x400, 0x800, 0x1000, 0x2000 |

拟合失败像素数：128×128 = 0/1024 微块、256×256 = 0/4096、512×512 = 0/16384（5 个尺寸全部零失配）。
注意：**A/B 掩码随宽度变化**，没有找到与宽度无关的闭式；这就是本段不采用“主机侧 swizzle
直写 tiled”的原因。若将来必须支持 tiled 上传，需再补 1024 宽等数据点，或改用“渲染式拷贝”。

## 3. 驱动改动

| 文件 | 改动 |
|------|------|
| `tu_image.cc` | `TILING_OPTIMAL` 且 `getenv("TU5XX_TILE")` 时才选 `TILE5_3`，默认 linear |
| `tu_clear_blit.h` | 新增 `tu5xx_cpu_copy_add_to_image / _run / _free` 声明 + `struct tu5xx_cpu_copy` 前向声明 |
| `tu_cmd_buffer.h` | `struct tu_cmd_buffer` 增加 `struct tu5xx_cpu_copy *cpu_copies` |
| `tu_clear_blit.cc` | 实现 CPU 拷贝队列；`tu_CmdCopyBufferToImage2` 在 `chip == 5` 时只登记、不进 r2d 路径 |
| `tu_knl.cc` | `tu_queue_submit` 在 `knl->queue_submit` 之前跑 `tu5xx_cpu_copy_run` |
| `tu_cmd_buffer.cc` | `tu_cmd_buffer_destroy` / `tu_reset_cmd_buffer` 释放队列 |

CPU 拷贝支持：任意 mip level、`baseArrayLayer + depth` 层、`bufferRowLength` /
`bufferImageHeight` 跨距、`imageOffset`；只支持 `tile_mode == 0`（linear），
遇到 tiled 目标会 `mesa_logw` 一次并跳过。

## 4. 真机验证（`work/vktriangle/vkup.c`，回归用例 `vk_up`）

路径：`buffer(4x4 RGBA8, 64 字节) --vkCmdCopyBufferToImage--> image(4x4 OPTIMAL)
--采样--> RT(16x16 LINEAR) --读回统计`。

```
img 4x4 OPTIMAL mem=8192
img 16x16 LINEAR mem=8192
分布: W=64 R=64 G=64 B=64 其它=0 (总 256)
quad0 ( 3, 3)=RGBA(255,255,255,255) 期望 W
quad1 (12, 3)=RGBA(255,0,0,255) 期望 R
quad2 ( 3,12)=RGBA(0,255,0,255) 期望 G
quad3 (12,12)=RGBA(0,0,255,255) 期望 B
DONE
```

改动前同一探针：`FAIL: vkQueueWaitIdle => 0xfffffffc`（DEVICE_LOST，无任何像素输出）。

回归：**11 用例全通过**（新增 `vk_up`；`vk_tile` 依赖 `TU5XX_TILE=1`，回归表已支持 per-case `env`）。

## 5. 遗留（下一段）

1. **`vkCmdCopyImageToBuffer`（读回）仍不可用**，同样会撞 r2d 的 DEVICE_LOST。CPU 方案对读回
   不安全：记录期或提交前拷贝都取不到“本提交内 GPU 渲染结果”。可行路线是“内部渲染到线性
   staging 图像 + 提交完成后 CPU memcpy”，即先补一个内部 blit draw，再做 CPU 回读。
2. 同理 `vkCmdCopyImage`（image→image）、`vkCmdBlitImage` 在 5xx 上仍走 r2d → 必挂。
3. tiled 上传（`TU5XX_TILE=1` 时的 buffer→image）当前被跳过；若要支持，需按上文掩码表实现
   CPU swizzle（宽度相关）或改用渲染式拷贝。
