# M2.4 多 descriptor set — 结案

日期：2026-10-20　设备：红米 Note 4X / Adreno 506　驱动：tu5xx

## 结论

两个 descriptor set 各自有一个 binding 0，能分别取到自己的纹理，互不覆盖。

真机：左半屏 `(10,200,90)` 20808 像素（set0 的纹理 A）、右半屏 `(200,60,10)` 20808 像素
（set1 的纹理 B），非背景共 41616，两色。反证：把编号改回「只用 binding」后两边 41616 全变成
`(200,60,10)` 单色（后绑的 set1 覆盖了同一个单元）。

## 问题本质

a5xx 的 cat5 纹理指令里 `TEX/SAMP` 是**立即数单元号**，硬件用它索引 TPL1 状态块（每级 16 个单元）。
Vulkan 侧的 `sampled image` 由 `(set, binding)` 标识，两者不是同一个编号空间：

- 通用 `nir_lower_samplers` 用 `base_index = var->data.binding`，**丢掉 set**；
- 状态块写入侧此前也用 `unit = binding + array`，同样**不含 set**。

⇒ 两个 set 的 binding 0 落在同一个单元，后写入的状态覆盖前一个（探针反证正是这个现象）。

## 修复

编号公式统一为（`tu_descriptor_set.h`）：

```
unit = set * TU5XX_UNITS_PER_SET + binding + array_index        TU5XX_UNITS_PER_SET = 8，TU5XX_MAX_TEX = 16
```

即 set0 占 0..7、set1 占 8..15，正好用满 a5xx 的 16 个单元。

两处必须同时改，否则一边对一边错：

1. **着色器侧**（`tu_shader.cc`）：新增 `tu5xx_lower_samplers()`，照 `nir_lower_samplers.c` 实现，
   但 `base_index` 换成上面的公式；数组元素用 `texture_offset/sampler_offset` 动态加，并把动态索引
   `umin` 到本 set 窗口内（`TU5XX_UNITS_PER_SET - binding - 1`）。`chip == 5` 时替换掉原来的
   `nir_lower_samplers(shader)`。
2. **状态块侧**（`tu_cmd_buffer.cc` 的 `tu5xx_emit_textures`）：`unit = s * TU5XX_UNITS_PER_SET + b + a`，
   并在 `b >= 8` 时打 warning（会跨进下一个 set 的窗口）。

探针里的描述符 dump 直接印证了编号：

```
tu5xx tex unit=0 set=0 bind=0 off=0: ... 020d0000 00020000 | samp 00000920 ...
tu5xx tex unit=8 set=1 bind=0 off=0: ... 020d2000 00020000 | samp 00000920 ...
```

同一 binding 号、不同单元号、不同地址。

## 探针 `vk_set`（`work/vktriangle/vkset.c` + `spirv_set2.h`）

- 两张 2×2 纯色纹理 A `(10,200,90)`、B `(200,60,10)`；
- 两个 set layout 内容相同（都是 binding 0 的 combined image sampler），pipeline layout 挂两个 set；
- 两个 descriptor set 分别写 A、B；`vkCmdBindDescriptorSets(firstSet=0, count=2, {A,B})` 一次绑两个；
- 两条管线：FS 分别声明 `set=0,binding=0` 与 `set=1,binding=0`（`gen_spirv_exp.py set2`），
  各裁剪半屏，一次提交两次 draw。

## 真机结果（`work/out/set.log`）

```
texA = (10,200,90) pitch=256     texB = (200,60,10) pitch=256
nonbg=41616  不同颜色数=2
  RGB( 10,200, 90) x 20808 bbox x[26..127]  y[26..229]
  RGB(200, 60, 10) x 20808 bbox x[128..229] y[26..229]
left(set0)  ( 76,128) = (10,200,90)   right(set1) (178,128) = (200,60,10)
```

反证（`work/out/set_neg.log`，编号退回只用 binding）：

```
tu5xx tex unit=0 set=0 bind=0 ...  tu5xx tex unit=0 set=1 bind=0 ...
nonbg=41616  不同颜色数=1
  RGB(200, 60, 10) x 41616
left(set0) = (200,60,10)  right(set1) = (200,60,10)
```

## 回归

`vk_set` 已入期望表；全量 **8/8 PASS**（vktri_base / vk_alpha_half / vk_mrt2 / vk_tex /
vk_tex2 / vk_tex2_linear / vk_mip / vk_set），`--mutate --case vk_set` 报 FAIL。

## 限制与遗留

- **每 set 只有 8 个绑定**、最多 2 个 set（8×2 = 16 单元）。binding ≥ 8 会跨窗口，运行时会打 warning
  但不会自动重映射；真实应用若用「set0 里十几个 binding」的布局会超限。
- 动态数组索引的 `umin` 只保证不越出本 set 窗口，语义上等价于「钳到最后一个元素」，与 Vulkan
  要求的未定义行为范围一致，但不是精确模拟。
- 未做：`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/storage、push descriptor、动态偏移
  （`vkCmdBindDescriptorSets` 的 `pDynamicOffsets` 目前忽略）。
- 未验证 descriptor set 在 draw 之间重新绑定（第二次 `vkCmdBindDescriptorSets` 覆盖）时的状态刷新。
