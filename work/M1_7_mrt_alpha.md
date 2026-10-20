# M1.7：颜色字节序 / alpha / MRT 验证（2026-10-20）

## 结论

**驱动侧无需改动**：双颜色附件（MRT）、alpha 写入、RGBA 通道顺序全部一次通过。H2c 修好顶点取数后，颜色输出链路本身是完整的。

## 测试程序

`work/vktriangle/vkmrt.c`（2 个 256×256 R8G8B8A8_UNORM 附件，linear tiling + host visible，同一 draw 写入）。
着色器由 `gen_spirv_exp.py` 新增两模式生成：

- `mrt2`：`loc0 = vec4(col.rgb, 1.0)`，`loc1 = vec4(col.b, col.g, col.r, 1.0)`（R/B 置换，用于鉴别路由）
- `alpha_half`：`loc0 = vec4(col.rgb, 0.5)`

编译：`cp spirv_$m.h spirv_exp.h && gcc -O1 -o vk_$m vkmrt.c -I/home/umeko/tu5xx -I. -ldl`

## 结果

`vk_mrt2`（两附件均有内容）：

| 探针 | att0 | att1（期望 R/B 互换） |
|---|---|---|
| (128,50) | RGBA(224,15,16,**255**) | RGBA(**16**,15,**224**,255) |
| (50,210) | RGBA(25,212,19,255) | RGBA(**19**,212,**25**,255) |
| (206,210) | RGBA(25,17,213,255) | RGBA(**213**,17,**25**,255) |
| (128,160) | RGBA(87,83,85,255) | RGBA(**85**,83,**87**,255) |

`nonbg`：att0=20808、att1=20808（与 H2c 单附件时一致）。

`vk_alpha_half`（FS 只写 loc0）：
- att0 全部探针 alpha = **128**（0.5 → 127.5 四舍五入），RGB 与 mrt2 的 att0 逐点相同
- att1 `nonbg=0`，保持背景 RGBA(0,0,0,255) —— 未写的附件不被 FS 触碰，符合预期

## 附注

- 本测试用 `VK_FORMAT_R8G8B8A8_UNORM` + `COLOR_SWAP=0`，通道顺序与解析预测逐点一致 ⇒ M1.4 遗留的"fd5 用 WZYX 而 tu5xx 用 0 需核对"一项，对本格式**不需要 swap**；换 B8G8R8A8 等格式时需再验。
- 驱动侧 `num_rts`（`MAX2(attachment_count,1)`）与 `SP/RB_FS_OUTPUT_CNTL.MRT` 已在 `tu_pipeline.cc:3333-3436` 实现，实测有效。
- 运行时 dmesg 有一条 `mdp5_irq_error_handler: errors: 04000000`，属显示控制器（MDP5）既有噪声，与 kgsl/GPU 提交无关。
