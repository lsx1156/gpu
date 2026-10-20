# M2.2：多纹理单元 + 过滤模式（2026-10-20）

## 结论

单 set 内多个 combined image sampler 各自独立可用，纹理单元号 **等于 SPIR-V binding 号**（不是「第几个纹理描述符」）。

探针 `vktex2.c`：两个 binding（**0 和 2，binding 1 故意留空**），FS 按 `uv.x < 0.5` 选边，
左半屏用 binding0 的 2×2 纹理，右半屏用 binding2 的另一套四色纹理：

```
tu5xx tex unit=0 set=0 bind=0 off=0  : ... 020d0000 ... | samp 00000920 02002000
tu5xx tex unit=2 set=0 bind=2 off=128: ... 020d1000 ... | samp 00000920 02002000
nonbg=41616  不同颜色数=4
  RGB(255,255,255) x 10404  x[26..127]  y[26..127]    <- unit0 texel(0,0)
  RGB(  0,120, 60) x 10404  x[128..229] y[26..127]    <- unit2 texel(1,0)
  RGB( 60,220, 70) x 10404  x[26..127]  y[128..229]   <- unit0 texel(0,1)
  RGB( 10, 10, 10) x 10404  x[128..229] y[128..229]   <- unit2 texel(1,1)
```

左右两半分别取自两张纹理，四个色块等面积、无交叉污染。**若仍按「顺序编号」发单元 0/1，则**
binding2 的状态会落到 unit1，而着色器取的是 unit2 ⇒ 右半屏应为黑。实测不是黑 ⇒ 单元号映射正确。

## 驱动改动：`tu5xx_emit_textures()` 重写

原来按「枚举到的第 n 个纹理描述符」发 `NUM_UNIT=n`，隐含「binding 从 0 连续」的假设，遇到
UBO 与纹理混排或空 binding 就会错位。现在：

- 单元号 `unit = binding 号`（`sl->binding[b]` 的下标就是 binding 号，`binding_count = max(binding)+1`，
  空槽 `type = 0` 被跳过）；
- 每个单元单独发一个包（`NUM_UNIT=1`、`DST_OFF=unit`），采样器包与纹理常量包各一；
- `TPL1_FS_TEX_COUNT = max(unit)+1`；
- array 描述符（`array_size>1`）展开成 `binding + a`。

代价是每单元 2 个包而非 1 个，a5xx 单 draw 纹理数很小，可接受；换来的是编号无假设。
`unit >= TU5XX_MAX_TEX(16)` 时打警告并丢弃（ir3 立即数形式要求单元号小）。

## 采样器语义实测

| 模式 | 采样器设置 | 结果 |
|---|---|---|
| `./vk_tex2` | NEAREST / CLAMP_TO_EDGE | 4 个纯色块，各 10404 像素 |
| `./vk_tex2 linear` | LINEAR / CLAMP_TO_EDGE | 双线性渐变：白 (255,255,255) 中心平台 2601 像素，其后逐列 (255,254,254)→(255,252,252)→(254,250,250)→(254,247,248)→(253,242,242)…，每列 51 像素 |
| `./vk_tex2 repeat` | NEAREST / REPEAT | 与 CLAMP 逐像素相同——**属预期**：uv 全域在 [0,1] 内，REPEAT 与 CLAMP 只在越界处不同 |

⇒ magFilter/minFilter 位（`SAMP_0` XY_MAG/XY_MIN）编码正确；`SAMP_1` 的 MIN/MAX_LOD 仍取自
M2.1 的 0.125 下限。**wrap 模式的越界行为未验证**（需要把 uv 缩到 [0,2)），记为未确认项。

## 探针改动

- `vktex2.c`：两张 2×2 纹理、两个 binding、`argv[1]` 选 `linear` / `repeat`；
  直方图槽位 8 → 64，并给每个色块带 bbox（这样「象限是否规整」直接从输出判定）。
- `gen_spirv_exp.py` 新增 `tex2` 模式：两个 `OpTypeSampledImage` 变量（binding 0 / 2），
  两条 `OpImageSampleImplicitLod`，`OpCompositeExtract` 取 `uv.x`，`OpFOrdLessThan` + `OpSelect` 选边。
  生成 `spirv_tex2.h`。

## 未做

- **多 descriptor set**：着色器索引目前 = set 内 binding 号，两个 set 都用 binding 0 会撞号；
  要做需要在 shader 侧按 pipeline layout 重映射（`unit = 前面各 set 宽度之和 + binding`）。
- 越界 uv 的 REPEAT / MIRROR / BORDER 行为。
- mipmap（`MIPLVLS>0`、LOD 选择、各 level 地址）、各向异性、比较采样器。
- tiled 纹理（仍强制 `TILE5_LINEAR`）、VS 侧采样（`SB4_VS_TEX`）。
