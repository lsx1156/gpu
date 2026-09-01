# GPU 开发平台（骁龙 625 / Adreno 506）

针对高通骁龙 625 移动平台的 GPU 驱动与图形开发仓库。硬件为远程触屏移动终端（Ubuntu/Mobian 系），主要工作聚焦两块：

1. **GPU 图形链路落地**：原生 EGL/GLES（Mesa Freedreno）、Chromium 渲染、Vulkan 自研后端（tu5xx）。
2. **交叉编译与设备部署**：在 PC（Windows）上交叉编译 Mesa 及自研驱动并部署到设备。

> 详细技术档案见 [`GPU_DEVELOPMENT.md`](GPU_DEVELOPMENT.md)，阶段侦察见 `RECON_PHASE0.md`。

## 硬件与驱动栈

| 项 | 值 |
|---|---|
| SoC | 高通骁龙 625（MSM8953） |
| GPU | Adreno 506 |
| 内核 | 6.7.5 |
| 操作系统 | Ubuntu/Debian 移动版（Mobian 系） |
| 图形会话 | Xorg + openbox |

| 层级 | 组件 | 状态 |
|---|---|---|
| 内核侧 | `msm` / `kgsl` DRM | 正常 |
| 原生 GLES/EGL | Mesa Freedreno（FD506） | **可用**，`glmark2` 200+ FPS |
| Vulkan | **自研 tu5xx 后端** | M1.4 已像素级验证三角形渲染 |
| 2D 加速 | XRender / EXA | 可用 |

## 关键约束（勿动）

- **Mesa 必须锁 24.0.5**（26.1.7 与内核 6.7.5 不兼容，`DRM_MSM_GEM_INFO` 返回 -22）。
- 禁止升级 Mesa、禁止添加 kisak PPA。
- Chromium 只能 CPU 软渲染，启动需带
  `--disable-gpu-watchdog --disable-gpu-compositing --disable-background-networking --use-angle=swiftshader`。
- Web 缩放用 `zoom`，禁用 `transform: scale()`。

## 目录结构

```
├── GPU_DEVELOPMENT.md        开发技术档案（降级命令 / 验证命令 / FAQ）
├── RECON_PHASE0.md           交叉编译环境阶段侦察报告
├── cross.ps1                 一键交叉编译封装
├── work/
│   ├── mesa-src/             Mesa 24.0.5 源码（含 A5XX 特化改动）
│   ├── mesa-build/           交叉编译产物
│   ├── vktriangle/           Vulkan 测试桩 + 命令流解析工具
│   ├── cmp_fd5_tu.py         命令流逐寄存器对比工具
│   └── deploy_tu5xx.ps1      驱动/测试程序部署脚本
└── patches/
    └── mesa-24.0.5-tu5xx/    自研改动的镜像快照（同步源）
```

## 里程碑

- **M0**：PC↔设备交叉编译环境打通（MSYS2 + ARM 官方工具链 + 设备 sysroot）。
- **M1.0**：Mesa 24.0.5 交叉编译打通。
- **M1.1**：A506 物理设备枚举打通。
- **M1.2**：A5XX 模板 variant 编入 turnip，内核提交链路验证。
- **M1.3**：第一个 `vkCmdDraw` 三角形执行无 fault。
- **M1.4**：三角形**像素读回验证通过**（`center=RGBA(0,255,0,255)`），得自自研 tu5xx 后端。

详细里程碑说明见 [`GPU_DEVELOPMENT.md`](GPU_DEVELOPMENT.md) 相应章节。

## 设备访问与部署

- 设备：`umeko@192.168.10.50`（密码 `1234`），仅支持目视观察触摸屏。
- 通道：PuTTY `plink` / `pscp`。

```sh
# 部署 tu5xx 驱动到设备
powershell -File work\deploy_tu5xx.ps1

# 设备端运行 vktriangle（抓取 RD dump）
sh /home/umeko/run15.sh
```

## 验证命令

见 [`GPU_DEVELOPMENT.md`](GPU_DEVELOPMENT.md#七验证命令速查) 的验证命令速查（驱动信息、Mesa 版本、DRM 节点、GLES 基准等）。

## 许可

本仓库含自研改动、测试桩与解析工具；`work/mesa-src` 引用上游 Mesa 24.0.5（MIT 许可），仅作本地研究用途。