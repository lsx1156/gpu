# tu5xx 路线图 v2（2026-10-20 制定）

依据：M1.4/M1.5/M1.7/M2.1/M2.2 已真机像素级验证（58 个提交）。本文件替代
`GPU_DEVELOPMENT.md` 里过期的「未来里程碑预期」表，后者保留历史行。

## 0. 判断口径

| 目标 | 可行性 | 说明 |
|---|---|---|
| 自研 a5xx Vulkan 后端，跑通探针与简单图形程序 | **可行，已过半** | 核心链路（draw / varying / MRT / 纹理）全部真机验证 |
| 端点级真实应用（SDL/Vulkan 简单 3D、UI） | **可行，需 M2.3+M2.4** | 门槛项是 mipmap 与多 set |
| 替代 blob 跑真实 3D 游戏 | **不可行** | 需 tiled rendering + 大量 Vulkan 特性，工作量 10× 以上 |
| 通过 VK-GL-CTS / dEQP | **不可行** | 上万用例，涉及 tiling/barrier/queries/compute/多队列 |

**性能现状**：只走 `TU_DEBUG=sysmem`，即每帧所有 render target 走系统内存 + 显式 resolve。
功能可用，带宽是 tiled 路径的数倍。性能是否"有意义"由 M3.x 决定。

## 1. 里程碑

记号：验收全部是**可判定的像素级判据**（色块面积/bbox/颜色），不接受"看起来对"。

### M2.0 回归套件 ✅ 2026-10-20
- 目标：一条命令在设备上跑 `vktri_base` / `vk_alpha_half` / `vk_mrt2` / `vk_tex` / `vk_tex2 [linear|repeat]`，
  按内置期望表判定 PASS/FAIL，失败时把日志拉回 `work/out/`。
- 为什么先做：后面每个里程碑都会碰 `tu_shader.cc` 与 `tu_cmd_buffer.cc`，
  没有自动回归就只能靠人眼比对直方图，历史上已因此浪费过一整轮排查。
- 验收：故意回退一处（如 `dword1` 写回 size-1）时脚本必须报 FAIL。
- 落地：`tools/tu5xx_regress.py` + 期望表 `tools/tu5xx_expect.json`。
  用法：`python tools/tu5xx_regress.py --pwfile <Windows 路径的 pwfile>`；
  自检：`--mutate`（把某色块计数 +1，必须报 FAIL）；`--list` 看用例表。
- 实测：6/6 PASS（vktri_base / vk_alpha_half / vk_mrt2 / vk_tex / vk_tex2 / vk_tex2_linear）；
  `--mutate --case vk_tex` → `色块 RGB(255,255,255) 计数 10404 != 期望 10405`，退出码 1 ✔

### M2.3 mipmap 链（2–3 天，门槛项）
- 目标：`MIPLVLS > 0`、per-level 地址推导、`minLod/maxLod`/LOD bias、`VkImageView` 的 `baseMipLevel/levelCount`。
- 前置取证（**不得凭记忆**）：`a5xx/fd5_texture.c` 的 mip 地址公式与 `fd5_emit.c` 里 level 地址进状态块的方式。
  a5xx 的 TPL1 用固定公式按 level 递增基址（对比 a6xx 有 per-level 表），具体字段必须读源码补进
  `work/A5XX_TEX_SPEC.md`。
- 验收：8×8 四色棋盘，四级 mip（8/4/2/1，各级预先填成整块不同颜色），
  FS 用 `OpImageSampleExplicitLod` 固定 LOD=0/1/2/3 各一次 draw，读回四个整屏纯色且与 level 颜色一致。
- 风险/回退：若硬件 mip 布局语义取不到，退化为「只在 level 0 采样 + 明确文档化限制」，
  并在 `VkImageCreateInfo.mipLevels>1` 时给应用告警。

### M2.4 多 descriptor set（1–2 天）
- 目标：两个 set 都用 binding 0 时不再撞号。
- 主方案（不依赖 layout 前缀和）：`unit = set * TU5XX_UNITS_PER_SET + binding`，
  `TU5XX_UNITS_PER_SET = 8`，总单元上限 16（`TPL1_FS_TEX_COUNT` 的小整数要求）。
  着色器侧在 `tu_shader.cc` 的 chip==5 路径用自写 pass 把 `(descriptor_set, binding)` 映射成 unit，
  取代裸 `nir_lower_samplers()`；发射端同步改 `DST_OFF`。
- 备选（若需紧凑编号）：按 pipeline layout 的前缀和分配，需确认 `tu_shader_compile` 能拿到 layout。
- 验收：两 set 各含 binding0 的 2×2 纹理（四色不同），FS 左右半屏各取一个 set，四块颜色正确；
  再加 `vk_tex2` 原例（单 set，binding 0/2）回归不退化。

### M2.5 越界 wrap 语义（0.5–1 天）
- 目标：REPEAT / MIRROR_REPEAT / CLAMP_TO_BORDER 在 uv ∉ [0,1] 时正确。
- 做法：探针把 uv 乘 2（新增 SPIR-V 模式 `tex2x2repeat`，`OpFMul` + float 常量 2.0），
  2×2 纹理平铺成 4×4 色块；CLAMP 会拉边、REPEAT 会重复、MIRROR 会镜像，三者直方图形态不同。
- 验收：三种模式输出三套可区分的直方图（色块数 16 / 拉边形态 / 镜像形态）。
- 风险：低（编码已按 `fd5_texture.c` 写死，测的只是验证）。

### M3.1 tiled 纹理（3–5 天，性能第一块）
- 目标：`VK_IMAGE_TILING_OPTIMAL` 走 tiled layout（TILE5_3 或等价），linear 仅作回退。
- 前置：`fdl/freedreno_layout.h` 的 tile 描述 + `fd5_texture.c` 的 TILE_MODE/SWAP/UBWC 选择。
- 验收：同一张 64×64 纹理分别以 linear/tiled 上传，采样结果逐像素相同（用 CRC/直方图对比）；
  再用带宽可感的场景（多次重绘 1080p 全屏纹理）记录耗时差。
- 风险：高。swizzle 一错就是错色/错位，且错法不直观 —— 验收必须先做「linear vs tiled 一致性」。

### M3.2 tiled rendering / GMEM（5–10 天，风险最高）
- 目标：去掉 `TU_DEBUG=sysmem`，走 binning + GMEM + resolve。
- 内容：binning pass、tile 描述符、`RB_` 系列的 tile 配置、resolve/clear 的 GMEM 版本、与 sysmem 的开关切换。
- 验收：既有全部探针在 GMEM 路径下输出与 sysmem 逐像素一致；1080p 简单场景帧时间下降。
- 回退：保留 sysmem 为默认，GMEM 作为 `TU_DEBUG=gmem` 可选路径；**功能优先**。

### M3.3 真实应用端到端（2–4 天）
- 目标：一个非探针程序跑通（SDL2 + Vulkan 旋转立方体，或 Vulkan 版 UI 合成）。
- 验收：连续运行 10 分钟无 fault、无 GPU hang；记录 fps 与帧时间分布。
- 这一步产出的数据是「性能是否有意义」的唯一判据。

### M4.x 可选（按需，不排期）
- VS 侧采样（`SB4_VS_TEX`）、SSBO/compute（a5xx 有 SP，但 turnip 侧缺 compute 管线）、
  shader disk cache、queries/timestamp、多子 pass、`VK_EXT_` 扩展子集。

## 2. 并行轨道（每个里程碑都做）

| 轨道 | 内容 |
|---|---|
| 镜像纪律 | 每完成一个里程碑：`work/mesa-src` → `patches/`，`git add -f` 文档与探针，提交并 push |
| 文档 | 每里程碑一份 `work/M*.md`：结论、根因、证据（含日志文件名）、未确认项 |
| 规格 | 新增寄存器语义一律进 `work/A5XX_TEX_SPEC.md`，逐字段带 `(文件:行号)`，无出处写「未确认」 |
| 技能 | 踩过的坑与判据进 skill `tu5xx-gpu-driver`（含 `references/`） |
| 推送通道 | github.com git/https 不通 ⇒ `work/gh_push_docs.py`（api.github.com + blob/tree/commit + PATCH refs） |

## 3. 排期（按依赖与风险）

| 顺序 | 项 | 估时 | 交付判据 |
|---|---|---|---|
| 1 | M2.0 回归套件 | 0.5 天 | 回退一处能被脚本抓到 |
| 2 | M2.3 mipmap | 2–3 天 | 四级 LOD 整屏纯色正确 |
| 3 | M2.4 多 set | 1–2 天 | 两 set 同 binding 0 各取各的纹理 |
| 4 | M2.5 越界 wrap | 0.5–1 天 | 三套可区分直方图 |
| 5 | M3.1 tiled 纹理 | 3–5 天 | linear/tiled 采样一致性 |
| 6 | M3.2 GMEM | 5–10 天 | 全部探针逐像素一致 + 帧时间下降 |
| 7 | M3.3 真实应用 | 2–4 天 | 10 分钟稳定 + fps 数据 |

近期三项（1–4）合计约 4–6 个工作日，做完即可支撑「端点真实应用」。
中期（5–6）是性能门槛，风险集中在这里，允许触发回退方案。

## 4. 已知硬约束

- Mesa 固定 24.0.5，不升级（设备上三库已无 libgallium 依赖，升级面过大）。
- 只做 a5xx；A6XX 代码路径不得因 tu5xx 改动而退化（回归含 M1 探针即为此）。
- 单点人力，`delegate_task` 并发上限 1，并行度靠批处理而非多 agent。
- 凭据不进命令行、不落明文；plink/pscp 用 `-pwfile`（Windows 路径，用后删除）。
