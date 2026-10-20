# M2.1：纹理采样打通（2026-10-20）

## 结论

**a5xx（Adreno 506）Vulkan 后端首次采到真实 texel。** 2×2 RGBA8 纹理、NEAREST、CLAMP_TO_EDGE，
screen 上四个象限各 10404 像素、颜色与 texel 一一对应、边界无渗色：

```
nonbg=41616  不同颜色数=4
  RGB(255,255,255) x 10404 bbox x[26..127]  y[26..127]   <- texel(0,0)
  RGB(230, 40, 50) x 10404 bbox x[128..229] y[26..127]   <- texel(1,0)
  RGB( 60,220, 70) x 10404 bbox x[26..127]  y[128..229]  <- texel(0,1)
  RGB( 80, 90,240) x 10404 bbox x[128..229] y[128..229]  <- texel(1,1)
```

（`y` 为帧缓冲行号，向下增大；texel 行序因此看起来是翻的，属正常 readback 方向。）

## 采样为 0 的四个真实根因

前一轮「FS 跑了 20808 像素但采样全 0」，逐条定位到四处独立缺陷，改一处不够：

1. **着色器侧：turnip 把 `tex` 的 deref 降级成 bindless handle。**
   `tu_shader.cc: lower_tex()` 调 `build_bindless()` 把纹理/采样器 deref 换成
   `nir_tex_src_texture_handle` / `sampler_handle`（a6xx 的描述符缓冲寻址），
   ir3 折叠常量后生成的立即数是**描述符缓冲偏移**，不是单元号。
   实测反汇编是 `sam ... s#1, t#0`（NIR 里两者都是 0），采样器打到不存在的单元 1。
   修法：`chip == 5` 时 `lower_tex()` 直接 `return false` 跳过 handle 降级，
   随后跑通用 pass `nir_lower_samplers()`（`base_index = var->data.binding`），
   得到 `sampler_index/texture_index = SPIR-V binding`，ir3 生成 `s#N, t#M` 立即数形式。

2. **两个状态包的 STATE_TYPE 不同。**
   采样器用 `ST4_SHADER`(0)，纹理常量用 `ST4_CONSTANTS`(1)，同属 `SB4_FS_TEX`(4)。
   原先两包都写 `ST4_SHADER`，纹理常量被当成着色器常量丢弃。

3. **`A5XX_TEX_CONST_1` 的 WIDTH/HEIGHT 是像素数本身，不是 size-1。**
   `fd5_texture.c:189-190` 写 `u_minify(prsc->width0, lvl)`。原先写 `width-1`。

4. **`MIN_LOD`/`MAX_LOD` 不能写 0。**
   radix8 定点，`fd5_texture.c:112-118` 取 `MIN2(max_lod, 0.125f)` 且下限 0.125；
   写 0 会让 level0 的 min/mag 选择退化。改为下限 0.125（值 0x20）。

## 实现要点（A5XX 与 A6XX 的模型差异）

| 项 | A6XX（上游 turnip） | A5XX（本后端） |
|---|---|---|
| 纹理/采样器寻址 | bindless 描述符缓冲 + handle | 固定单元号，直接取 SPIR-V binding |
| 状态推进 | 描述符缓冲 + `CP_LOAD_STATE6` | `CP_LOAD_STATE4` 直接模式推 TPL1 状态块 |
| 描述符槽 | a6xx 16 dword tex const + 4 dword samp | 每槽前 12 dword 放 a5xx tex const，偏移 16 起放 4 dword sampler |

draw 时发两组包（`tu5xx_emit_textures()`，`tu_cmd_buffer.cc`）：

- `CP_LOAD_STATE4(DST_OFF=0, SRC=SS4_DIRECT, BLOCK=SB4_FS_TEX, NUM_UNIT=n, TYPE=ST4_SHADER)` + n×4 dword 采样器
- `CP_LOAD_STATE4(DST_OFF=0, SRC=SS4_DIRECT, BLOCK=SB4_FS_TEX, NUM_UNIT=n, TYPE=ST4_CONSTANTS)` + n×12 dword 纹理常量
- `TPL1_FS_TEX_COUNT = n`

单元号 = binding 号；`n < 16` 才能保证 ir3 用立即数形式（`ir3_cp.c:653-660`）。

## 改动文件

| 文件 | 改动 |
|---|---|
| `tu_shader.cc` | chip==5 跳过 bindless 降级 + 追加 `nir_lower_samplers()` |
| `tu_device.h` / `tu_device.cc` | `struct tu_sampler` 增 `VkSamplerCreateInfo info`，`tu_sampler_create` 保存 |
| `tu_descriptor_set.cc` | 新增 `tu5xx_write_tex_descriptor()` / `tu5xx_write_sampler_descriptor()` / `tu5xx_tex_wrap()`，在 combined image sampler 写入路径调用 |
| `tu_cmd_buffer.cc` | 新增 `tu5xx_emit_textures()`，在 `tu6_draw_common()` 内 `CHIP==A5XX` 时调用 |

规格取证与位段表见 `work/A5XX_TEX_SPEC.md`（27.5 KB，含未确认项清单）。

## 测试程序

`work/vktriangle/vktex.c`：2×2 RGBA8 纹理（linear、host visible、`vkGetImageSubresourceLayout` 取 pitch），
`layout(binding=0) sampler2D`，FS 输出 `texture(tex, v_uv)`；顶点色通道借作 uv。
改成本轮的四色象限版（6 顶点两三角形铺满 uv 0..1），并把直方图扩成带 bbox，
这样「四个 texel 是否各自到位」可直接从输出判定。

运行：`VK_ICD_FILENAMES=... TU_DEBUG=sysmem MESA_SHADER_CACHE_DISABLE=true ./vk_tex`

## 回归

| 程序 | 结果 |
|---|---|
| `vktri_base`（M1.4 基础三角形） | nonbg=20808，三点探针 224/15/16、25/212/19、25/17/213 ✔ |
| `vk_alpha_half`（M1.7 alpha） | att0=20808、att1=0 ✔ |
| `vk_mrt2`（M1.7 MRT） | att0=20808、att1=20808 ✔ |

`tu_shader.cc` 的改动对所有 draw 生效，回归即验证其未破坏非纹理路径。

## 尚未做（M2.1 之后）

- 多纹理/多 set：单元号现在等于 binding 号且只处理 `SB4_FS_TEX`；多 set 需要先做「binding → 全局单元号」映射（Vulkan 的 set 内 binding 会撞号）。
- mipmap / 各向异性 / 比较采样器（未验证 `MIPLVLS>0` 与大 LOD）。
- tiled 纹理（当前强制 `TILE5_LINEAR`，`TILE5_3` 的 swizzle 未碰）。
- VS 侧采样（`SB4_VS_TEX` 未发）。
