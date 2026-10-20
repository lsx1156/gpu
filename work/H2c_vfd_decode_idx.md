# H2c 根因：a5xx VFD_DECODE 的 IDX 必须是**属性槽位**，不是 binding（2026-10-20）

## 结论（一句话）

第 3 分量（蓝）从来不是 varying 递送问题，而是**顶点取数**问题：`A5XX_VFD_DECODE_INSTR_IDX()` 用了 `attr->binding`，交错顶点缓冲（1 个 binding、2 个属性）时两个 DECODE 都指向 `VFD_FETCH[0]`，于是**颜色属性整份读到的是位置数据**。红/绿两通道"看起来在插值"其实是 pos.x / pos.y 被夹到 [0,1] 后的结果。

修复：`IDX(attr->binding)` → `IDX(loc)`（a5xx 是"每属性槽一个 VFD_FETCH"，IDX 必须与 FETCH 下标一致，同 fd5）。

## 1. 分离实验（先证伪"varying 递送"假设）

用 `work/vktriangle/gen_spirv_exp.py` 生成三个 SPIR-V 变体，设备上 `gcc -O1 -o vktri_$m vktriangle.c -I/home/umeko/tu5xx -I. -ldl`，同一 .so 跑：

| 变体 | 做法 | 读回 |
|---|---|---|
| `base` | 原样：VS 传 vec3 颜色 → FS 输出 vec4(col,1) | R≈0、G 随位置变、**B 恒 0** |
| `fs_const_blue` | FS 完全不读 varying，直接输出 (0,0,1,1) | 三角形内**全 (0,0,255)** |
| `vs_const_blue` | VS 仍读颜色属性，但把 varying 第 3 分量强制 1.0 | **B 处处 255**，R/G 与 base 逐点相同 |

→ 输出/读回通道对 comp2 正常；**VPC → FS 的 comp2 递送也正常**。问题被夹到 VS 输入端。

## 2. RD 证据（同一次 submit，tu-dump）

```
FETCH0.BASE_LO 0x020c3000  SIZE 0x48 STRIDE 0x18   (pos,  +0)
FETCH1.BASE_LO 0x020c300c  SIZE 0x3c STRIDE 0x18   (color,+12)
DECODE0        0xc7400000        <- IDX=0
DECODE1        0xc7400000        <- IDX=0  ✗ 应指向 FETCH[1]
DEST_CNTL0     0x00000007        (r0, mask 7)
DEST_CNTL1     0x00000047        (r1.x, mask 7)  ✓
```

VFD_FETCH 的基址/大小/stride、格式 0x74（R32G32B32）、DEST_CNTL regid/mask、SP_VS_OUT、VPC_VAR、VPC_PACK **全部正确**——唯一缺陷是 DECODE 的 IDX。

## 3. fd5 金参考（`work/fd5_glsmooth3.rd`，Xorg submit，NUMNONPOSVAR=2）

```
DECODE0 0xc4720000   IDX=0
DECODE1 0xc4620001   IDX=1   <- 每属性一个 FETCH 槽，IDX=槽号
```

## 4. 修复

`patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_pipeline.cc:2600`（同步 `work/mesa-src/.../tu_pipeline.cc`）：

```c
- A5XX_VFD_DECODE_INSTR_IDX(attr->binding) |
+ A5XX_VFD_DECODE_INSTR_IDX(loc) |
```

重编后 `DECODE1 = 0xc7400001`（IDX=1），与 fd5 一致。

## 5. 验证（像素读回 vs 解析重心坐标预测）

三角形顶点（NDC → 屏幕）：红 (0,-0.8)→(128,26) 顶尖、绿 (-0.8,0.8)→(26,230) 左下、蓝 (0.8,0.8)→(230,230) 右下。

| 探针 | 解析预测 | 实测 |
|---|---|---|
| (128,50) 顶尖附近 | (225, 15, 15) | **(224, 15, 16)** |
| (50,210) 左下 | (25, 212, 18) | **(25, 212, 19)** |
| (206,210) 右下 | (25, 18, 212) | **(25, 17, 213)** |
| y=160, x=128 中线 | (87, 84, 84) | **(87, 83, 85)** |

全部落在 ±1 LSB。`nonbg` 18207 → 20808，`vktriangle clean exit`，`dmesg` 0 行。

## 6. 教训

- **"分量丢失"先怀疑数据来源，再怀疑通路。** 三个分量里只有两个"对"，是因为只有一个属性读错了，不是递送少了分量。
- 隔离实验比读代码快：常量 FS（测输出+读回）与常量 varying（测递送）两发就夹死了故障域。
- `de4f18e` 记的「A5XX bary.f inloc>=2 限制」**不成立**：FS 一直发 3 条 bary.f，机制本身正常。
- 单 binding 单属性时 `IDX(binding)` 与 `IDX(loc)` 恰好等价——所以这个 bug 只在**多属性交错缓冲**下暴露。
