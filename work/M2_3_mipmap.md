# M2.3 mipmap 链采样 — 结案

日期：2026-10-20　设备：红米 Note 4X / MSM8953 / Adreno 506　驱动：`libvulkan_freedreno.so`（tu5xx）

## 结论

mip 链通了，且**每级地址由硬件按 a5xx 规则自动寻址**，驱动只需给出 base 级描述符 + `MIPLVLS`，
本驱动不改寄存器、不加级表。真机判据：8×8 RGBA8 四级 mip、每级一色，四个 LOD 各画一个象限，
四象限各 10404 像素、颜色与级别一一对应、无渗色。

## 做了什么

### 1. 布局改用 fdl5（关键）

`tu_image.cc`：`chip == 5` 时

- `tile_mode = TILE6_LINEAR`、`ubwc_enabled = false`（此前是 a6xx 默认 `TILE6_3`，仅因 2×2 尺寸小而未暴露）；
- 布局计算改调 **`fdl5_layout()`**，不再用 `fdl6_layout()`。

fdl5 的每级规则（`src/freedreno/fdl/fd5_layout.c:63-104`）：

```
pitchalign = cpp_shift + 6            # cpp=4 -> 8，即 pitch 256B 对齐
pitch(l)   = align(minify(width0,l) * cpp, 1<<pitchalign)
nblocksy(l)= minify(height0,l)        # tile_mode=0 时；末级 align(nblocksy,32) 防 over-fetch
size0(l)   = nblocksy(l) * pitch(l)
offset(0)  = 0 ; offset(l+1) = offset(l) + size0(l)
layer_size = align(total, 4096) ; size = layer_size * array_size
```

8×8 RGBA8 四级实测：`off = 0 / 2048 / 3072 / 3584`，`pitch` 全 256，`layer_size = 12288`。

### 2. 描述符

`tu_descriptor_set.cc`：

- `MIPLVLS = view.level_count - 1`（硬件语义：除 base 外还有几级）。dword0 实测 `0x0c036880`
  → `MIPLVLS=3` 正确。
- **地址修正**：原代码 `view.base_addr + view.offset` 是**重复计数**——`base_addr` 在
  `fdl6_view.c:203-204` 已是 `iova + fdl_surface_offset(baseMip, baseLayer)`。改为只用 `base_addr`，
  否则 baseMip>0 的视图会错位（当前探针 baseMip=0，offset=0，故此前未暴露）。
- `WIDTH/HEIGHT/PITCH` 本来就取自 `view`（= base 级 minify 后的尺寸与 `fdl_pitch(baseMip)`），无需改。

### 3. 采样器

`fd5_texture.c:83-118` 的语义搬过来：

- `mipmapMode == LINEAR` 才置 `MIPFILTER_LINEAR_NEAR`，NEAREST 不置（`samp[0]=0x920` 与 M2.1 一致）。
- 有 mip 链（`maxLod > 0`）时用真实 `MIN_LOD/MAX_LOD`；`maxLod == 0` 时仍压到 `<= 0.125`
  （M2.1 的 level0 退化根因不能丢）。实测 `samp[1]=0x00030000` → `MAX_LOD = 3.0`。

### 4. 探针 `vk_mip`（`work/vktriangle/vkmip.c` + `spirv_lod.h`）

- 8×8 RGBA8、`mipLevels=4`，四级四色 `(200,30,40)/(40,190,60)/(50,70,220)/(230,200,40)`；
- 每级用 `vkGetImageSubresourceLayout` 问驱动要 offset/pitch 再写，顺便验证驱动布局（期望值打印在旁）；
- FS 用 **`OpImageSampleExplicitLod (0x0058)` + ImageOperands mask `0x2` (Lod)**，常量 LOD 0..3，
  由 `gen_spirv_exp.py lod spirv_lod.h` 生成 4 个 FS 变体；
- 4 条管线（FS 不同 + 象限裁剪），一次提交画 4 个 draw，单次回读。

生成/编译：
```
python gen_spirv_exp.py lod spirv_lod.h
gcc -O1 -o vk_mip vkmip.c -I/home/umeko/tu5xx -I. -ldl
```

## 真机结果（`work/out/mip.log`）

```
tu5xx tex unit=0 ... : 0c036880 00040008 20008002 00000003 020d0000 00020000 | samp 00000920 00030000
  tex mem req: size=12288
  L0: off=0    pitch=256   8x8      L1: off=2048 pitch=256 4x4
  L2: off=3072 pitch=256   2x2      L3: off=3584 pitch=256 1x1
  nonbg=41616  不同颜色数=4
    RGB(200, 30, 40) x 10404 bbox x[26..127]  y[26..127]
    RGB( 40,190, 60) x 10404 bbox x[128..229] y[26..127]
    RGB( 50, 70,220) x 10404 bbox x[26..127]  y[128..229]
    RGB(230,200, 40) x 10404 bbox x[128..229] y[128..229]
  quad0(LOD0) = (200,30,40)  quad1(LOD1) = (40,190,60)
  quad2(LOD2) = (50,70,220)  quad3(LOD3) = (230,200,40)
  DONE
```

## 伪证（反向验证）

把 `MIPLVLS(mip_levels)` 改成 `MIPLVLS(0)` 重建部署后：

```
RGB(200, 30, 40) x 41616 bbox x[26..229] y[26..229]
quad0..quad3 全部 = (200,30,40)
0 通过 / 1 失败 (共 1)
```

⇒ 四个 LOD 全落回 level 0，说明 `MIPLVLS` 确实是硬件寻址的开关，且各级地址/数据放置正确。
恢复后 7/7 PASS。

## 回归

```
[PASS] vktri_base / vk_alpha_half / vk_mrt2 / vk_tex / vk_tex2 / vk_tex2_linear / vk_mip
7 通过 / 0 失败
```

注意：本轮**改动了所有图像的布局**（a6xx TILE6_3 → a5xx linear + fdl5），六个旧探针仍逐像素通过，
说明此前是"小尺寸巧合成立"，现在与硬件语义一致。

## 未确认 / 遗留

- 各向异性过滤、`MIPFILTER_LINEAR_NEAR` 的线性 mip 混合未见逐像素数据（本轮只测 NEAREST 选级）。
- 3D 纹理/数组纹理的 `ARRAY_PITCH`、`MIN_LAYERSZ` 未测（`fdl5_layout` 的 `is_3d` 分支与 a5xx 硬件
  auto-sizer 的"层大小不再缩小"行为未验证）。
- `vkCmdCopyBufferToImage` 路径未接：探针仍是"映射内存 + 驱动给的 layout"直接写。
- 非 4 字节 cpp（如 R8、RGBA16F）的 pitchalign 规则（cpp<4 且 tiled 才用 +7）未测，当前全 linear ⇒ 走 +6。
