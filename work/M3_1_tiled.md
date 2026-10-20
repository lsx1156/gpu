# M3.1 tiled 纹理 — 结案（第一段：tiled 渲染目标 + tiled 采样往返）

日期：2026-10-20　设备：红米 Note 4X / Adreno 506　驱动：tu5xx

## 目标与判据

M3.1 的目标不是「让 tiled 能跑」，而是**让 tiled 路径与 linear 路径输出逐像素一致**。可判定判据：

1. 同一份内容分别经 tiled 往返（渲染进 tiled → 当纹理采样）与 linear 直采，两张 256×256 输出**逐字节相同**；
2. tiled 图像的四象限颜色与像素数正确（4 色各 16384）。

## 代码改动（三处，缺一不可）

| 文件 | 改动 |
|---|---|
| `tu_image.cc` | a5xx 的 `VK_IMAGE_TILING_OPTIMAL` 且 `cpp>=4` ⇒ `tile_mode = TILE5_3`；其余仍 `TILE6_LINEAR`（数值 0，与 a5xx 同义）。UBWC 保持关闭。`cpp<4` 的 tiled 需 `pitchalign+1`，本轮不启用。 |
| `tu_cmd_buffer.cc` | `A5XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(...)` 由硬编码 `TILE5_LINEAR` 改为取 `fdl_tile_mode(&iview->image->layout[0], base_mip_level)`。 |
| `tu_descriptor_set.cc` | a5xx 纹理常量 dword0 的 `TILE_MODE` 同样改为取布局的 tile mode（原先硬编码 linear）。 |

关键点：**a5xx 的 tile mode 只有一个非零档（TILE5_3）**。源码里 a5xx 只出现 `TILE5_LINEAR`/`TILE5_2`/`TILE5_3`，`TILE5_2` 仅用于 GMEM 渲染目标（`fd5_gmem.c:63`），纹理/普通资源用 `TILE5_3`（`fd5_blitter.c:483`）。`fdl5_layout()` 本身不解释 tile 尺寸，只按「非零 ⇒ cpp<4 时 pitchalign+1」调整对齐，所以 tiled/linear 的 pitch 差异全部来自对齐规则：

- `128×128 RGBA8 TILE5_3` ⇒ `pitch = align(128*4=512, 256) = 512`，`size = align(128*512, 4096) = 65536`
- `256×256 RGBA8 LINEAR` ⇒ `pitch = 1024`，`size = 262144`（与 M2.x 一致）

## 探针 `vktile.c`（`work/vktriangle/`）

三次提交，每次提交后 `vkWaitForFences` 全同步，绕开本后端尚不完整的 barrier 语义：

- **s1**：2×2 LINEAR 纹理（白/红/绿/蓝）NEAREST 采样 → 渲染进 **128×128 TILING_OPTIMAL** 图像 T；
- **s2**：把 T 当 **tiled 纹理** NEAREST 采样 → 渲染进 256×256 LINEAR 图像 A（tiled 往返）；
- **s3**：2×2 纹理直接采样 → 渲染进 256×256 LINEAR 图像 B（linear 参考）；
- 读回 A、B 全像素比较，并统计差异的分通道分布与包围盒。

## 真机结果（`work/out/tile2.log`）

```
img[0] TILED  128x128 mem=65536 offset=0 pitch=512 size=65536
img[1] LINEAR 256x256 mem=262144 offset=0 pitch=1024 size=262144
img[2] LINEAR 256x256 mem=262144 offset=0 pitch=1024 size=262144
A(tiled 往返) vs B(linear 直采): 差异字节=0 最大差=0 平均差(仅差异)=0.00
  分通道 R=0 G=0 B=0 A=0  bbox x[999..0] y[999..0]
A quad0 ( 64, 64) = (255,255,255)  期望 W
A quad1 (192, 64) = (255,  0,  0)  期望 R
A quad2 ( 64,192) = (  0,255,  0)  期望 G
A quad3 (192,192) = (  0,  0,255)  期望 B
A 不同颜色数=4   （四色各 16384）
```

## 反证（证明判据有牙）

只把**描述符侧**的 `TILE_MODE` 退回 0（linear），RB 侧仍按 TILE5_3 写：

```
差异字节=65536 最大差=255
  分通道 R=0 G=32768 B=32768 A=0  bbox x[0..255] y[0..255]
A quad1 (192, 64) = (255,255,255)  期望 R      ← 右半两象限颜色错位
A quad3 (192,192) = (  0,255,  0)  期望 B
```

即：tiled 写 + linear 读 ⇒ 右半屏两象限错色、65536 字节差异；两侧都按布局走 ⇒ 0 差异。**说明 TILE5_3 在 RB 与纹理单元两侧真的对称，不是「两边都当 linear 所以碰巧一致」。**

## 回归

`vk_tile` 入期望表（7 项：`DONE`、`TILED +128x128 mem=65536`、`差异字节=0 最大差=0`、四色各 16384、quad0/1/3 位置颜色）。全量 **10/10 PASS**；`--mutate --case vk_tile` rc=1（判据可失败）。

## 未覆盖（M3.1 第二段）

- **主机侧直写 tiled 图像**（不经 RB）：需要知道 TILE5_3 的像素内 swizzle 与 tile 尺寸；本轮用「GPU 渲染进 tiled」绕开了它。`vkCmdCopyBufferToImage` 目前仍无实现，真实应用上传纹理会撞这里。
- `cpp<4` 的 tiled（R8/R8G8）、`TILE5_2`（GMEM 渲染目标，属 M3.2）。
- tiled + mip 链组合、tiled + linear 过滤（需 uv 越界/插值边界数据）。
- 尺寸非 2 的幂、非 64 对齐的 tiled 图像（当前 128×128 恰好整块）。
