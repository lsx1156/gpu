# M2.5 越界 wrap 语义 — 结案

日期：2026-10-20　设备：红米 Note 4X / Adreno 506　驱动：tu5xx

## 结论

三套 addressMode 的越界取样语义**已经正确**，本轮无需改驱动：`CLAMP_TO_EDGE` /
`REPEAT` / `MIRRORED_REPEAT` 在真机上的颜色游程与规范逐像素吻合。M2.5 由「待做」转为「验证 + 入回归」。

## 探针 `vk_wrap`（`work/vktriangle/vkwrap.c`）

- 2×2 NEAREST 纹理：`(0,0)` 白、`(1,0)` 红、`(0,1)` 绿、`(1,1)` 蓝，linear pitch 256B；
- 四边形 uv：`u ∈ [-1.5, 2.5)`（跨 4 个 u 单位 / 204 像素），`v` 固定 0.25 ⇒ 只落在第 0 行，可判颜色只用 W/R；
- 三条横带各用一个 sampler（同一张纹理、三个 descriptor set），**每条带重新 `vkCmdBindDescriptorSets`**；
- 判据：每条带中间行做颜色游程（RLE），比较块宽与交替规律。

## 真机结果（`work/out/wrap.log`）

```
nonbg=41616  不同颜色数=2      RGB(255,255,255) x 20808   RGB(255,0,0) x 20808
CLAMP  y= 60 RLE: W x102 R x102
REPEAT y=128 RLE: R x25 W x26 R x25 W x26 R x26 W x25 R x26 W x25
MIRROR y=196 RLE: R x51 W x51 R x51 W x51
```

## 逐条推导核对

u 与像素的映射：`u = -1.5 + 4*(x-26)/204`，即 1 个 u 单位 = 51 像素、0.5 单位 = 25.5 像素。

**CLAMP_TO_EDGE**：`u<0` 与 `u>1` 都钳到边界。`u=0.5` 落在 x=128，于是前 102 像素取 texel0(W)、后 102 像素取 texel1(R)。观察 `W x102 R x102` ✔。

**REPEAT**：周期 1，`u` 每 0.5 换一个 texel，共 8 段 ≈25/26 像素。起点 `u=-1.5 ⇒ mod 1 = 0.5 ⇒ texel1=R`，故 `R W R W R W R W`。观察完全一致 ✔。

**MIRRORED_REPEAT**：周期 2，`r = mod(u,2)`，`r>1` 时取 `2-r`，再 `floor(m*2)`：

- `u∈[-1.5,-0.5) ⇒ r∈[0.5,1.5) ⇒ m∈[0.5,1] ⇒ texel1 = R`（**整段 1 个单位都是 R**，不是每 0.5 换）
- `u∈[-0.5,0.5) ⇒ r∈[1.5,2)∪[0,0.5) ⇒ m∈[0,0.5) ⇒ texel0 = W`
- 之后每 1 个单位交替

观察 `R x51 W x51 R x51 W x51`（4 段各 51 像素）✔。注意与 REPEAT 的区别不在颜色顺序而在**块宽**（51 vs 25/26），这正是可判据。

> 校正：初版按「每 0.5 单位镜像」推的期望 `W R R W` 是错的，真机数据反过来修正了推导。

## 附带验证

探针在一条命令缓冲里三次 `vkCmdBindDescriptorSets` 覆盖同一个 set 索引，三条带的 sampler 各自生效 ⇒ 记录期重新绑定 set 后状态块会刷新（M2.4 遗留的「重绑刷新」项由此覆盖）。

## 回归

`vk_wrap` 已入期望表（5 项：`DONE` + `nonbg=41616` + 三条 RLE 正则，块宽用 `1[0-9][0-9]` / `2[0-9]` / `5[0-9]` 容差）。全量 **9/9 PASS**。

## 遗留

- `BORDER`（`VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER`）未测：需要 borderColor 通路，a5xx 的 `SP_PS_2D_BORDER_COLOR`/`TPL1_BORDER_COLOR` 未接。
- `MIRROR_CLAMP_TO_EDGE` 未测（只用了一个 u 单位延伸的场景不足以区分）。
- 各向异性过滤、`maxAnisotropy`、`compareEnable`（shadow sampler）未接。
- 线性过滤 + 越界组合（M2.1 只测了 [0,1] 内的线性）未测。
