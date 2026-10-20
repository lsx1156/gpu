# Adreno 506 (a5xx / fd5) 最简 2D 纹理采样规格

来源：Mesa 24.0.5，`D:\project\gpu\work\mesa-src\mesa-24.0.5`
目标场景（全部字段均按此场景给值）：
- `VK_FORMAT_R8G8B8A8_UNORM`（= `PIPE_FORMAT_R8G8B8A8_UNORM`）
- 2D、单层、单 mip、linear layout（`TILE5_LINEAR`）
- `VK_FILTER_NEAREST`（mag/min 都是 NEAREST）、`VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`
- 一张纹理一个 sampler，`NUM_UNIT = 1`

文中所有 `文件:行号` 均相对 `mesa-24.0.5/`。`未确认` 表示源码中没有依据，禁止凭记忆补。

---

## 0. 总览：一次采样要写几个包

纹理采样状态在 a4xx/a5xx 里都在同一个状态块 `SB4_FS_TEX`（片元）/ `SB4_VS_TEX`（顶点）里，用 `CP_LOAD_STATE4` 直接模式推进：

| 内容 | 单元大小 | STATE_TYPE | STATE_BLOCK(FS) | 出处 |
|---|---|---|---|---|
| 采样器（texsamp0..3） | 4 dwords | `ST4_SHADER`(0) | `SB4_FS_TEX`(4) | `src/gallium/drivers/freedreno/a5xx/fd5_emit.c:343-350` |
| 纹理描述符（texconst0..11） | 12 dwords | `ST4_CONSTANTS`(1) | `SB4_FS_TEX`(4) | `src/gallium/drivers/freedreno/a5xx/fd5_emit.c:370-377` |

即 **同一个状态块、两种 STATE_TYPE、两个包**。枚举值：`SB4_VS_TEX=0`、`SB4_FS_TEX=4`（`src/freedreno/registers/adreno/adreno_pm4.xml:734,738`），`ST4_SHADER=0`、`ST4_CONSTANTS=1`（同文件 `:757-758`），`SS4_DIRECT=0`（同文件 `:762`），`CP_LOAD_STATE4=0x30`（同文件 `:372`）。

`CP_LOAD_STATE4` 的 dword0 位段（`adreno_pm4.xml:765-770`）：`DST_OFF[13:0]`、`STATE_SRC[17:16]`、`STATE_BLOCK[21:18]`、`NUM_UNIT[31:22]`；dword1 `STATE_TYPE[1:0]`、`EXT_SRC_ADDR[31:2] shr 2`（`:771-774`）；dword2 `EXT_SRC_ADDR_HI[31:0]`（`:775-777`）。

`DST_OFF` 是**以状态单元为单位**的偏移（`adreno_pm4.xml:710-720` 的示例里 `DST_OFF = 16` 用于第 16 个单元起），我们永远从 0 开始。

---

## 1. 纹理描述符 12 dwords 逐字段表

场景：RGBA8_UNORM / 2D / 单 mip / 单层 / linear。

### dword0 — `A5XX_TEX_CONST_0`（`a5xx.xml:2923`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `TILE_MODE` | [1:0] | 0 = `TILE5_LINEAR` | 字段 `a5xx.xml:2924`；枚举 `a5xx.xml:58-62`（`TILE5_LINEAR=0`,`TILE5_2=2`,`TILE5_3=3`）；驱动发射时才 OR 进去 `fd5_emit.c:388-389` |
| （保留） | [3] | 0 | `a5xx.xml:2924-2925` 之间无定义 ⇒ 无字段 |
| `SRGB` | [2] | 0（UNORM，非 sRGB） | `a5xx.xml:2925`；只有 sRGB 格式才置位 `fd5_texture.c:169-171` |
| `SWIZ_X` | [6:4] | 0 = `A5XX_TEX_X` | `a5xx.xml:2926`；枚举 `a5xx.xml:2908-2915`（X=0,Y=1,Z=2,W=3,ZERO=4,ONE=5） |
| `SWIZ_Y` | [9:7] | 1 = `A5XX_TEX_Y` | `a5xx.xml:2927` |
| `SWIZ_Z` | [12:10] | 2 = `A5XX_TEX_Z` | `a5xx.xml:2928` |
| `SWIZ_W` | [15:13] | 3 = `A5XX_TEX_W` | `a5xx.xml:2929` |
| `MIPLVLS` | [19:16] | 0（= 只有 1 级） | `a5xx.xml:2930`；取值语义 = `last_level - first_level` `fd5_texture.c:184-185,188` |
| `SAMPLES` | [21:20] | 0 = `MSAA_ONE` | `a5xx.xml:2931`；枚举 `adreno_common.xml:310-311`；`fd5_texture.c:152` |
| `FMT` | [29:22] | 0x30 = `TFMT5_8_8_8_8_UNORM` | `a5xx.xml:2932`；枚举值 `a5xx.xml:166`；映射 `fd5_format.c:180`（`_T/VT` 宏 `fd5_format.c:53-58`） |
| `SWAP` | [31:30] | 0 = `WZYX` | `a5xx.xml:2933`；枚举 `adreno_common.xml:343-344`；fd5 只在 X24S8 特例里改成 `XYZW` `fd5_texture.c:165-167`，正常格式恒为 0 |

swizzle 的合成方式：`util_format_compose_swizzles(desc->swizzle, view_swiz)`，RGBA8 的 `desc->swizzle` 是恒等，所以 SWIZ 就是 0,1,2,3（`fd5_format.c:423-440`）。
=> dword0 = `0x30<<22 | 0<<4 | 1<<7 | 2<<10 | 3<<13` = **0x0C006880**

### dword1 — `A5XX_TEX_CONST_1`（`a5xx.xml:2935`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `WIDTH` | [14:0] | `width`（**写宽度本身，不是 width-1**） | 字段 `a5xx.xml:2936`；`fd5_texture.c:189` 直接写 `u_minify(prsc->width0, lvl)` |
| `HEIGHT` | [29:15] | `height` | 字段 `a5xx.xml:2937`；`fd5_texture.c:190` |
| （保留） | [31:30] | 0 | 无字段定义 |

注意：字段只有 15 位 ⇒ 宽/高上限 32767（`a5xx.xml:2936-2937`）。

### dword2 — `A5XX_TEX_CONST_2`（`a5xx.xml:2939`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `PITCHALIGN` | [3:0] | 2（= `log2(pitchalign)-6`，pitchalign=256B ⇒ 8-6=2） | 字段 `a5xx.xml:2949`；驱动写 `rsc->layout.pitchalign - 6` `fd5_texture.c:191` |
| `BUFFER` | [4] | 0（不是 buffer texture） | `a5xx.xml:2947`；仅 `PIPE_BUFFER` 置位 `fd5_texture.c:179` |
| （保留） | [6:5] | 0 | 无字段定义 |
| `PITCH` | [28:7] | `align(width*4, 256)`（字节 = 行 stride） | 字段 `a5xx.xml:2951`（注释 "Pitch in bytes (so actually stride)" `a5xx.xml:2950`）；`fd5_texture.c:192` 用 `fd_resource_pitch()` |
| `TYPE` | [31:29] | 1 = `A5XX_TEX_2D` | 字段 `a5xx.xml:2952`；枚举 `a5xx.xml:2916-2922`（1D=0,2D=1,CUBE=2,3D=3,BUFFER=4）；`fd5_tex_type()` 映射 `fd5_texture.c:196` |

### dword3 — `A5XX_TEX_CONST_3`（`a5xx.xml:2954`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `ARRAY_PITCH` | [13:0] `shr=12` | `layer_size >> 12` | 字段 `a5xx.xml:2961`；`fd5_texture.c:202` 写 `rsc->layout.layer_size` |
| （保留） | [22:14] | 0 | 无字段定义 |
| `MIN_LAYERSZ` | [26:23] `shr=12` | 0（只有 3D 才用） | 字段 `a5xx.xml:2962`；只在 `PIPE_TEXTURE_3D` 分支赋值 `fd5_texture.c:216-219` |
| `TILE_ALL` | [27] | 0（"makes all levels have tiling"，我们单级+linear） | `a5xx.xml:2965-2968` |
| `FLAG` | [28] | 0 | `a5xx.xml:2969`；fd5 从未置位 |
| （保留） | [31:29] | 0 | 无字段定义 |

### dword4 — `A5XX_TEX_CONST_4`（`a5xx.xml:2971`）：地址低 32 位

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `BASE_LO` | [31:5] `shr=5` | `(base_iova >> 5) & 0x7FFFFFF`（即 iova 位 [31:5]） | 字段 `a5xx.xml:2972`；发射代码 `fd5_emit.c:397-398` |
| （保留/未定义） | [4:0] | 写 0 ⇒ 要求 **base 32 字节对齐** | `a5xx.xml:2972` 从 bit5 起 ⇒ 低位不属于描述符 |

### dword5 — `A5XX_TEX_CONST_5`（`a5xx.xml:2974`）：地址高位 + DEPTH

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `BASE_HI` | [16:0] | `(base_iova >> 32) & 0x1FFFF` | 字段 `a5xx.xml:2975` |
| `DEPTH` | [29:17] | 1（2D 单层） | 字段 `a5xx.xml:2976`；`fd5_texture.c:203` `A5XX_TEX_CONST_5_DEPTH(1)` |
| （保留） | [31:30] | 0 | 无字段定义 |

### dword6..11 — `A5XX_TEX_CONST_6..11`（`a5xx.xml:2978-2983`）

这 6 个 dword 在 xml 里**只有 reg32 声明、没有任何位段定义**（`a5xx.xml:2978-2983`），字段含义**未确认**。
fd5 驱动对非 buffer 纹理从不给它们赋值（结构体来自 `CALLOC_STRUCT` `fd5_texture.c:132`，之后仅写 `texconst0..5`，见 `fd5_texture.c:151-225`），发射时原样写出（`fd5_emit.c:403-408`）⇒ **全部写 0**。

---

## 2. 采样器 4 dwords 逐字段表

### dword0 — `A5XX_TEX_SAMP_0`（`a5xx.xml:2867`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| `MIPFILTER_LINEAR_NEAR` | [0] | 0（无 mip 过滤） | `a5xx.xml:2868`；只在 `min_mip_filter==LINEAR` 时置位 `fd5_texture.c:90-95` |
| `XY_MAG` | [2:1] | 0 = `A5XX_TEX_NEAREST` | `a5xx.xml:2869`；枚举 `a5xx.xml:2848-2852`（NEAREST=0,LINEAR=1,ANISO=2）；`tex_filter()` `fd5_texture.c:63-75`，写入 `:96` |
| `XY_MIN` | [4:3] | 0 = `A5XX_TEX_NEAREST` | `a5xx.xml:2870`；`:97` |
| `WRAP_S` | [7:5] | 1 = `A5XX_TEX_CLAMP_TO_EDGE` | `a5xx.xml:2871`；枚举 `a5xx.xml:2853-2859`（REPEAT=0,CLAMP_TO_EDGE=1,MIRROR_REPEAT=2,CLAMP_TO_BORDER=3,MIRROR_CLAMP=4）；`fd5_texture.c:99` |
| `WRAP_T` | [10:8] | 1 | `a5xx.xml:2872`；`fd5_texture.c:100` |
| `WRAP_R` | [13:11] | 1（2D 不用 R，但同值无副作用） | `a5xx.xml:2873`；`fd5_texture.c:101` |
| `ANISO` | [16:14] | 0 = `A5XX_TEX_ANISO_1` | `a5xx.xml:2874`；枚举 `a5xx.xml:2860-2866`；`fd5_texture.c:82,98`（`max_anisotropy` 默认 1 ⇒ `util_last_bit(0)` = 0） |
| （保留） | [18:17] | 0 | 无字段定义 |
| `LOD_BIAS` | [31:19]，fixed radix 8 | 0（有符号 0 也是 0） | `a5xx.xml:2875`（注释 "no idea how many bits for real"）；`fd5_texture.c:107` |

=> dword0 = `1<<5 | 1<<8 | 1<<11` = **0x00000920**

### dword1 — `A5XX_TEX_SAMP_1`（`a5xx.xml:2877`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| （保留） | [0] | 0 | 无字段定义（`COMPARE_FUNC` 从 bit1 起 `a5xx.xml:2878`） |
| `COMPARE_FUNC` | [3:1] | 0 = `FUNC_NEVER`（非 depth-compare，字段被忽略） | `a5xx.xml:2878`；枚举 `adreno_common.xml:21-30`；只在 `compare_mode` 时才写 `fd5_texture.c:121-123` |
| `CUBEMAPSEAMLESSFILTOFF` | [4] | 0（= 开启 seamless filter；2D 无影响） | `a5xx.xml:2879`；`COND(!seamless_cube_map, ...)` `fd5_texture.c:104` |
| `UNNORM_COORDS` | [5] | 0 | `a5xx.xml:2880`；`fd5_texture.c:105` |
| `MIPFILTER_LINEAR_FAR` | [6] | 0 | `a5xx.xml:2881`；fd5 从不置位 |
| （保留） | [7] | 0 | 无字段定义（`MAX_LOD` 从 bit8 起 `a5xx.xml:2882`） |
| `MAX_LOD` | [19:8]，ufixed radix 8 | 0x20 = 0.125 | `a5xx.xml:2882`；无 mip 过滤时写 `MIN2(max_lod, 0.125f)` `fd5_texture.c:112-118`（注释解释：需要一个略大于 0 的 LOD clamp，让硬件在 level 0 上决定 min/mag 过滤） |
| `MIN_LOD` | [31:20]，ufixed radix 8 | 0x20 = 0.125 | `a5xx.xml:2883`；同上 `fd5_texture.c:117-118` |

> **关键坑**：单 mip 的 NEAREST sampler 不能把 MIN_LOD/MAX_LOD 写 0。照 fd5 的做法写 0.125（radix8 ⇒ 0x20），否则硬件在 level 0 的 min/mag 选择上会退化（`fd5_texture.c:113-116` 注释）。

=> dword1 = `0x20<<20 | 0x20<<8` = **0x02002000**

### dword2 — `A5XX_TEX_SAMP_2`（`a5xx.xml:2885`）

| 字段 | 位段 | 本场景取值 | 出处 |
|---|---|---|---|
| （保留） | [6:0] | 0 | 无字段定义（`BCOLOR_OFFSET` 从 bit7 起 `a5xx.xml:2901`） |
| `BCOLOR_OFFSET` | [31:7] | 0（CLAMP_TO_EDGE，不用 border color） | `a5xx.xml:2901`；驱动写 `bcolor_offset + i` `fd5_emit.c:358-359`，单位 0x80 字节（`FD5_BORDER_COLOR_SIZE 0x80` `fd5_emit.c:180`） |

### dword3 — `A5XX_TEX_SAMP_3`（`a5xx.xml:2903`）

xml 里是 `<reg32 offset="3" name="3"/>`，**无任何位段定义**，含义**未确认**；fd5 恒写结构体里的 0（`fd5_emit.c:360`，结构体来自 `CALLOC_STRUCT` `fd5_texture.c:81`）⇒ **0**。

---

## 3. 12 dwords 里承载 64 位 base address 的两个 word

**位置：dword4 = 低 32 位，dword5 = 高 32 位（低在前）。**

发射代码（`fd5_emit.c:397-398`）：

```c
OUT_RELOC(ring, rsc->bo, view->offset, (uint64_t)view->texconst5 << 32, 0);
```

`OUT_RELOC` 在 a5xx 上是 **2 个 dword 的 64 位写入**（`src/freedreno/drm/freedreno_ringbuffer.h:312-343`，注释 "NOTE: OUT_RELOC() is 2 dwords (64b) on a5xx+"），实际值 = `(bo_iova + offset) | (orval)`（`freedreno_ringbuffer.h:298-310`），orval = `texconst5 << 32`。

于是这 64 位值 `V` 按小端拆成两个 dword：

| 发送顺序 | dword | 内容 | 位段依据 |
|---|---|---|---|
| 第 5 个 dword（index 4） | `A5XX_TEX_CONST_4` | `V[31:0]`，其中 `V[31:5]` = `BASE_LO` | `a5xx.xml:2972`（`BASE_LO` low=5 high=31 shr=5 ⇒ 低 5 位忽略，**iova 必须 32B 对齐**） |
| 第 6 个 dword（index 5） | `A5XX_TEX_CONST_5` | `V[63:32]` = `(iova>>32) | (texconst5)`；其中 bit[16:0]=`BASE_HI`，bit[29:17]=`DEPTH` | `a5xx.xml:2975-2976` |

即：

```
dword4 = base_iova & 0xFFFFFFFF          // 低 5 位会被硬件忽略（BASE_LO shr=5）
dword5 = (base_iova >> 32) | (depth << 17)
```

两个注意点（都可从位段直接推出）：
1. `DEPTH` 占 dword5 的 [29:17]，正好压在 iova 的 [61:49] 上。因此 **iova 必须 < 2^49**，否则 OR 会污染 DEPTH（这也是 `BASE_HI` 只有 17 位的原因，`a5xx.xml:2975`）。
2. `BASE_LO` 从 bit5 开始（`a5xx.xml:2972`）⇒ 纹理基地址必须 **32 字节对齐**；dword4 的低 5 位不属于描述符，写 0。

空纹理（无 view）时 fd5 直接写 `dword4=0, dword5=texconst5`（`fd5_emit.c:399-402`）。

---

## 4. ir3 a5xx tex 指令的 samp / tex 索引来源与状态块下标约定

### 4.1 指令字段位置

cat5 采样指令里两个立即数字段（`src/freedreno/isa/ir3-cat5.xml`）：

| 字段 | 位段 | 宽度 | 出处 |
|---|---|---|---|
| `SAMP` | instr[24:21] | 4 bit（`#cat5-samp size="4"`） | `ir3-cat5.xml:82-84`；类型定义 `ir3-cat5.xml:624-631` |
| `TEX` | instr[31:25] | 7 bit（`#cat5-tex size="7"`） | `ir3-cat5.xml:85-87`；类型定义 `ir3-cat5.xml:671-678` |

⇒ 硬件层面 sampler 索引上限 15、texture 索引上限 127。反汇编打印为 `, s#N` / `, s#N, t#M`（`ir3/ir3_print.c:396-399`）。

### 4.2 索引值的来源

1. NIR 阶段：`nir_tex_instr` 的 `tex->sampler_index` 与 `tex->texture_index`，各自 `create_immed_typed(..., TYPE_U16)`（`ir3/ir3_compiler_nir.c:2916-2929`）。
2. 打包成一个 vec2 参数：`info.samp_tex = ir3_collect(b, sampler, texture)` —— **sampler 在 .x，texture 在 .y**（`ir3_compiler_nir.c:2936`，函数注释 `ir3_compiler_nir.c:2797-2802` 说明这是 hvec2、可能动态也可能立即数）。
3. 生成指令时若走 `.s2en`（动态索引）形式，`ir3_cp.c:643-660` 会在两者都是立即数且 **`< 16`** 时把 `.s2en` 去掉，把值塞进立即数字段：

```c
struct ir3_register *samp = samp_tex->srcs[0];   // 注意顺序：srcs[0]=sampler
struct ir3_register *tex  = samp_tex->srcs[1];
if (... && (samp->iim_val < 16) && (tex->iim_val < 16)) {
   instr->flags &= ~IR3_INSTR_S2EN;
   instr->cat5.samp = samp->iim_val;
   instr->cat5.tex  = tex->iim_val;
}
```

（`ir3/ir3_cp.c:653-660`；`<16` 的条件在 `:656-657`）

结论：**自研后端最省事的路是保证 sampler 索引与 texture 索引都 < 16**，这样 cat5 指令就是普通的立即数形式（不带 `S2EN`），无需在着色器里为索引额外开寄存器。索引 ≥ 16 会留在 `.s2en` 动态形式，需要额外处理（`ir3_cp.c:637-645`）。

注意：`ir3_compiler_nir.c:2934` 有 `info.samp_idx = tex->texture_index;`（用 texture_index 赋给 samp_idx），这是 Mesa 侧的一个已知不一致，但它只用于 prefetch/记账（`info.samp_idx` 不参与 cat5 立即数的编码，见 `:2936-2938` 与 `ir3_cp.c:659-660`），不影响硬件字段。

另有一个会**改写 cat5.tex 的场景**：ASTC sRGB / `tg4`(gather4) 会把 tex 索引重映射到"备选纹理槽"（`ir3_compiler_nir.c:4503-4512`、`:4537-4546`）。最简后端**不要用 gather4 / ASTC**，否则驱动要多准备一组纹理槽。

### 4.3 状态块下标约定（是否等于 Vulkan binding）

- 硬件侧：`CP_LOAD_STATE4` 用 `DST_OFF(0)` + `NUM_UNIT(N)` 顺序推进（`fd5_emit.c:344-347`、`:371-374`），所以**第 i 个写入的状态单元 = 硬件单元 i**；shader 里 `SAMP=i` / `TEX=i` 就取第 i 个单元。
- 软件侧：gallium 的状态数组下标 `tex->textures[i]` / `tex->samplers[i]`（`fd5_emit.c:351-360`、`:378-382`）与 shader 里的采样器索引是同一个编号（`fd_sampler_states_bind` / `fd_set_sampler_views` 绑定，`fd5_texture.c:234,236`）。
- **Mesa 里没有任何"binding 号"概念**，GL 的"纹理单元/sampler 单元号"就是这个 i。Vulkan 后端因此**可以自由规定**：只要 (a) 把描述符写在第 `binding` 个单元，(b) 让 shader 的 `texture_index`/`sampler_index` 也等于 `binding`，两者就一致。建议直接让 **binding number == 状态块单元下标 == cat5 的 TEX/SAMP 字段值**，并保持在 0..15。

补充：顶点/片元是两个独立状态块（`SB4_VS_TEX=0` / `SB4_FS_TEX=4`，`adreno_pm4.xml:734,738`），同一个纹理在两个 stage 各要写一份。

---

## 5. TILE5_LINEAR 与线性 pitch 对齐的实际取值

### 5.1 tile_mode

- 枚举 `a5xx.xml:58-62`：`TILE5_LINEAR=0`、`TILE5_2=2`、`TILE5_3=3`。
- `TILE_MODE` 字段在 `A5XX_TEX_CONST_0[1:0]`（`a5xx.xml:2924`），**由 emit 阶段 OR 进去**：`view->texconst0 | A5XX_TEX_CONST_0_TILE_MODE(tile_mode)`，`tile_mode` 取自 `fd_resource(...)->layout.tile_mode`，无纹理时默认 `TILE5_LINEAR`（`fd5_emit.c:383-389`）。
- 参考：fd5 对"可 blit 的格式"默认给 `TILE5_3`（`fd5_blitter.c:476-486`），所以 Mesa 的普通 2D 纹理其实是 tiled；**我们要 linear，就把 `TILE_MODE` 填 0**。

### 5.2 pitch 对齐

`fdl5_layout()`（`src/freedreno/fdl/fd5_layout.c`）：

```c
if (layout->cpp < 4 && layout->tile_mode)        // :58
   layout->pitchalign = fdl_cpp_shift(layout) + 7;   // :59  128 * cpp 字节
else
   layout->pitchalign = fdl_cpp_shift(layout) + 6;   // :61  64 * cpp 字节
layout->pitch0 = align(layout->cpp * nblocksx, 1 << layout->pitchalign);  // :66
```

`fdl_cpp_shift` = `util_logbase2(cpp)`（`src/freedreno/fdl/freedreno_layout.h:216-226` 的 `fdl_level_linear`/`fdl_pitch` 一族的用法，`fd5_layout.c:57`）。

**RGBA8（cpp=4）⇒ cpp_shift=2 ⇒ pitchalign = 2+6 = 8 ⇒ pitch 对齐 256 字节**（与 tile_mode 无关，linear 也走同一分支）。

- 描述符字段：`PITCHALIGN = pitchalign - 6 = 2`（`fd5_texture.c:191`），`PITCH = fd_resource_pitch(rsc, 0)`（`:192`），即 `align(width*4, 256)`。
- 即：`pitch_bytes = align(width * 4, 256)`。
- 校验使用点：`fdl_pitch()` 返回 `align(pitch0, 1<<pitchalign)`（`freedreno_layout.h:158-162`），`fd_resource_pitch` 只是转发（`freedreno_resource.h:297-303`）。

### 5.3 分配大小（linear 也要按这个来，否则会越界）

`fdl5_layout()` 里额外两条（**对 linear 同样生效**）：

- 最后一级 mip 的 `nblocksy` 要向上对齐到 32 行（容忍硬件 over-fetch）：`fd5_layout.c:71-83`；
- `layer_size = align(size, 4096)`（`fd5_layout.c:95-109`；`freedreno_resource.c:1393-1395` 有同样兜底）。

⇒ 分配字节数应为 `align(align(height,32) * pitch_bytes, 4096)`，`ARRAY_PITCH = layer_size >> 12`。

例：256×256 RGBA8 → pitch=1024、size=262144、layer_size=262144、ARRAY_PITCH=64。
例：100×50 RGBA8 → pitch=512、`align(50,32)=64` → size=32768、layer_size=32768、ARRAY_PITCH=8。

---

## 6. 伪代码：生成 12+4 个 dword

```c
// 输入：base_iova（32B 对齐）、width、height（像素）、pitch_bytes（256B 对齐）
// 输出：tex[0..11]、smp[0..3]，以及两个 CP_LOAD_STATE4 包

static inline uint32_t align_u(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

void a5xx_make_tex_rgba8_unorm_linear(uint64_t base_iova,
                                      uint32_t width, uint32_t height,
                                      uint32_t pitch_bytes,   // = align_u(width*4, 256)
                                      uint32_t tex[12], uint32_t smp[4])
{
    /* ---- 分配大小（供调用者参考，不直接进描述符）----
     * size       = align_u(height, 32) * pitch_bytes
     * layer_size = align_u(size, 4096)
     */
    uint32_t layer_size = align_u(align_u(height, 32) * pitch_bytes, 4096);

    /* dword0 */
    tex[0] = 0;
    tex[0] |= (0u & 0x3)         << 0;   // TILE_MODE  = TILE5_LINEAR (0)
    tex[0] |= (0u & 0x1)         << 2;   // SRGB       = 0   (UNORM)
    tex[0] |= (0u & 0x7)         << 4;   // SWIZ_X     = A5XX_TEX_X (0)
    tex[0] |= (1u & 0x7)         << 7;   // SWIZ_Y     = A5XX_TEX_Y (1)
    tex[0] |= (2u & 0x7)         << 10;  // SWIZ_Z     = A5XX_TEX_Z (2)
    tex[0] |= (3u & 0x7)         << 13;  // SWIZ_W     = A5XX_TEX_W (3)
    tex[0] |= (0u & 0xf)         << 16;  // MIPLVLS    = 0   (1 level)
    tex[0] |= (0u & 0x3)         << 20;  // SAMPLES    = MSAA_ONE (0)
    tex[0] |= (0x30u)            << 22;  // FMT        = TFMT5_8_8_8_8_UNORM
    tex[0] |= (0u & 0x3)         << 30;  // SWAP       = WZYX (0)

    /* dword1 */
    tex[1] = (width  & 0x7fff) | ((height & 0x7fff) << 15);   // 写宽高本身

    /* dword2 */
    tex[2] = (2u)                          // PITCHALIGN = pitchalign-6 = 2 (256B)
           | ((pitch_bytes & 0x3fffff) << 7) // PITCH (bytes)
           | (1u << 29);                    // TYPE = A5XX_TEX_2D

    /* dword3 */
    tex[3] = (layer_size >> 12) & 0x3fff;   // ARRAY_PITCH (shr 12)

    /* dword4/5：64 位地址（低 32 位在前） */
    tex[4] = (uint32_t)(base_iova & 0xFFFFFFFF);         // BASE_LO = bits[31:5]
    tex[5] = (uint32_t)((base_iova >> 32) & 0x1ffff)     // BASE_HI
           | (1u << 17);                                 // DEPTH = 1 (2D)

    /* dword6..11：xml 无位段定义，fd5 恒写 0 */
    for (int i = 6; i < 12; i++) tex[i] = 0;

    /* ---- sampler ---- */
    smp[0] = (0u << 0)    // MIPFILTER_LINEAR_NEAR = 0
           | (0u << 1)    // XY_MAG = A5XX_TEX_NEAREST
           | (0u << 3)    // XY_MIN = A5XX_TEX_NEAREST
           | (1u << 5)    // WRAP_S = CLAMP_TO_EDGE
           | (1u << 8)    // WRAP_T = CLAMP_TO_EDGE
           | (1u << 11)   // WRAP_R = CLAMP_TO_EDGE
           | (0u << 14)   // ANISO  = ANISO_1
           | (0u << 19);  // LOD_BIAS = 0 (fixed radix 8)
    smp[1] = (0u  << 1)    // COMPARE_FUNC = FUNC_NEVER
           | (0u  << 4)    // CUBEMAPSEAMLESSFILTOFF = 0
           | (0u  << 5)    // UNNORM_COORDS = 0
           | (0u  << 6)    // MIPFILTER_LINEAR_FAR = 0
           | (0x20u << 8)  // MAX_LOD = 0.125 (ufixed radix 8)
           | (0x20u << 20); // MIN_LOD = 0.125
    smp[2] = 0;            // BCOLOR_OFFSET = 0
    smp[3] = 0;            // 无位段定义，fd5 恒 0
}

// 发射（stage = SB4_FS_TEX 或 SB4_VS_TEX；index = 状态块单元下标 = binding）
void a5xx_emit_tex_state(ring, unsigned sb, unsigned index,
                         const uint32_t tex[12], const uint32_t smp[4])
{
    // 采样器：4 dwords/unit，ST4_SHADER
    OUT_PKT7(ring, CP_LOAD_STATE4, 3 + 4);          // header = 0x70B00007
    OUT_RING(ring, DST_OFF(index) | STATE_SRC(SS4_DIRECT)
                 | STATE_BLOCK(sb) | NUM_UNIT(1));   // 本场景 index=0 ⇒ 0x00500000
    OUT_RING(ring, STATE_TYPE(ST4_SHADER) | EXT_SRC_ADDR(0));   // = 0
    OUT_RING(ring, 0);                              // EXT_SRC_ADDR_HI
    OUT_RING(ring, smp[0]); OUT_RING(ring, smp[1]);
    OUT_RING(ring, smp[2]); OUT_RING(ring, smp[3]);

    // 纹理：12 dwords/unit，ST4_CONSTANTS
    OUT_PKT7(ring, CP_LOAD_STATE4, 3 + 12);         // header = 0x70B0800F
    OUT_RING(ring, DST_OFF(index) | STATE_SRC(SS4_DIRECT)
                 | STATE_BLOCK(sb) | NUM_UNIT(1));   // 0x00500000
    OUT_RING(ring, STATE_TYPE(ST4_CONSTANTS) | EXT_SRC_ADDR(0)); // = 1
    OUT_RING(ring, 0);
    for (int i = 0; i < 12; i++) OUT_RING(ring, tex[i]);
}
```

包长度/头公式：`pm4_pkt7_hdr(opcode,cnt) = 0x70000000 | cnt | parity(cnt)<<15 | (opcode&0x7f)<<16 | parity(opcode)<<23`，`cnt` = 包头之后的 dword 数（`src/freedreno/common/freedreno_pm4.h:84-90`，`OUT_PKT7` 见 `src/freedreno/drm/freedreno_ringbuffer.h:390-395`）。⇒ 4 dword 采样器包 header = **0x70B00007**，12 dword 纹理包 header = **0x70B0800F**。

本场景数值示例（256×256，base=0x100000，pitch=1024，layer_size=262144）：

```
tex = 0C006880 00800100 20020002 00000040 00008000 00020000 00000000 00000000 00000000 00000000 00000000 00000000
smp = 00000920 02002000 00000000 00000000
```

---

## 7. 必需的全局 TPL1 寄存器

| 寄存器 | 地址 | fd5 写的值 | 出处 | 说明 |
|---|---|---|---|---|
| `TPL1_MODE_CNTL` | 0x0f02 | `0x00000544` | `a5xx.xml:1610`（注释 "always 00000544?"）；`fd5_emit.c:999-1000`（在 `fd5_emit_restore` 里） | **位含义未确认**（xml 无位段）。blitter 路径写 `0x344`（`fd5_blitter.c:178`） |
| `TPL1_TP_FS_ROTATION_CNTL` | 0xe764 | `0` | `a5xx.xml:2661`（无位段）；`fd5_emit.c:1053-1054`（`fd5_emit_restore`） | 位含义**未确认** |
| `TPL1_FS_TEX_COUNT` | 0xe750 | `num_textures`（有 SSBO/image 时写 `~0`） | `a5xx.xml:2649`（裸 uint，无位段）；`fd5_emit.c:855-857` | 不设会导致 FS 拿不到纹理 |
| `TPL1_VS_TEX_COUNT` | 0xe700 | `num_textures` | `a5xx.xml:2626`；`fd5_emit.c:846-847` | 同上（VS 采样时） |
| `TPL1_ADDR_MODE_CNTL` | 0x0f01 | **fd5 从不写**（全树 0 处写入） | `a5xx.xml:1609`；枚举 `adreno_common.xml:365-367`（`ADDR_32B=0`,`ADDR_64B=1`） | 语义**未确认**；本机 a5xx blob 的寄存器 dump 显示 `ADDR_64B=1`。如果你的地址走 64 位 iova，先按默认值试，失败再试置 1 |

`TPL1_TEX0_CONFIG`：**在 a5xx.xml 与整个 Mesa 源码树中都不存在**（全树 grep 0 命中），a5xx 的纹理配置全部在 `A5XX_TEX_CONST_*` 状态块里，没有这个寄存器。

---

## 8. 未确认项清单（源码无依据，禁止照抄）

1. `A5XX_TEX_CONST_6..11` 的字段含义：xml 只有 `reg32` 声明、无位段（`a5xx.xml:2978-2983`）；fd5 恒写 0。
2. `A5XX_TEX_SAMP_3` 全部位段：同上（`a5xx.xml:2903`）。
3. `TPL1_MODE_CNTL` 每个 bit 的含义（`a5xx.xml:1610` 只有 "always 00000544?" 注释）。
4. `TPL1_TP_FS_ROTATION_CNTL` 位含义（`a5xx.xml:2661` 无位段）。
5. `TPL1_FS_TEX_COUNT` / `TPL1_VS_TEX_COUNT` 是裸 uint（`a5xx.xml:2649,2626`），"计数"语义靠行为推断。
6. `TPL1_ADDR_MODE_CNTL` 的 32B/64B 语义（`adreno_common.xml:365-367` 只有枚举名）。
7. `WIDTH`/`HEIGHT` 是否 field 值 = 像素数还是 像素数-1：xml 无说明，Mesa 写的是**像素数本身**（`fd5_texture.c:189-190`），按此实现。
8. `LOD_BIAS` 的真实位宽：xml 自己写着 "no idea how many bits for real"（`a5xx.xml:2875`），按 [31:19] 用。
9. `SAMPLES` 在非 MSAA 下取 `MSAA_ONE`(=0)（`adreno_common.xml:310-311`），字段对普通纹理是否被硬件忽略：未确认。
10. `BCOLOR_OFFSET` 的单位（推断为 0x80 字节，依据 `FD5_BORDER_COLOR_SIZE 0x80` `fd5_emit.c:180` 与 `low=7` `a5xx.xml:2901`）：未确认。

---

## 9. 参考文件

| 文件 | 用途 |
|---|---|
| `src/freedreno/registers/adreno/a5xx.xml` | `A5XX_TEX_CONST_*`(2906-2984)、`A5XX_TEX_SAMP_*`(2846-2904)、`a5xx_tile_mode`(58-62)、`a5xx_tex_fmt`(147-166+)、`TPL1_*` |
| `src/freedreno/registers/adreno/adreno_common.xml` | `a3xx_msaa_samples`(310-311)、`a3xx_color_swap`(343-344)、`adreno_compare_func`(21-30)、`a5xx_address_mode`(365-367) |
| `src/freedreno/registers/adreno/adreno_pm4.xml` | `CP_LOAD_STATE4`(697-777)、`a4xx_state_block`(734-738)、`a4xx_state_type`(757-758) |
| `src/gallium/drivers/freedreno/a5xx/fd5_texture.c` | sampler/view 描述符拼装（63-126、151-225） |
| `src/gallium/drivers/freedreno/a5xx/fd5_emit.c` | 两个 `CP_LOAD_STATE4` 包（341-409）、`TPL1_*`（846-857、999-1000、1053-1054）、`FD5_BORDER_COLOR_SIZE`(180) |
| `src/gallium/drivers/freedreno/a5xx/fd5_format.c` | `_T/VT` 宏(49-66)、格式表(180)、`fd5_tex_swiz`(423-440) |
| `src/freedreno/fdl/fd5_layout.c` | pitchalign/pitch/size/layer_size（51-109） |
| `src/freedreno/fdl/freedreno_layout.h` | `fdl_pitch`(158-162)、`fdl_cpp_shift`(216-226) |
| `src/gallium/drivers/freedreno/freedreno_resource.h` | `fd_resource_pitch/offset`(297-312) |
| `src/freedreno/isa/ir3-cat5.xml` | `SAMP`[24:21]、`TEX`[31:25]（82-87、624-678） |
| `src/freedreno/ir3/ir3_compiler_nir.c` | 索引来源（2797-2802、2916-2938）、ASTC 重映射（4503-4512） |
| `src/freedreno/ir3/ir3_cp.c` | `.s2en` → 立即数折叠（637-670） |
| `src/freedreno/drm/freedreno_ringbuffer.h` | `OUT_RELOC` 64 位（298-343）、`OUT_PKT7`(390-395) |
| `src/freedreno/common/freedreno_pm4.h` | `pm4_pkt7_hdr`(84-90) |
