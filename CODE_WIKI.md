# GPU 项目 Code Wiki

> 仓库：`d:\project\gpu`（GitHub: https://github.com/lsx1156/gpu）
> 维护原则：工作时间节点（任务完成、需求变更）都 git commit 归档；每次小改动都归档
> 详细技术档案见 [GPU_DEVELOPMENT.md](GPU_DEVELOPMENT.md)，阶段侦察见 [RECON_PHASE0.md](RECON_PHASE0.md)

---

## 目录

1. [项目概述](#1-项目概述)
2. [整体架构](#2-整体架构)
3. [硬件与软件环境](#3-硬件与软件环境)
4. [关键约束与禁令](#4-关键约束与禁令)
5. [主要模块职责](#5-主要模块职责)
6. [tu5xx 驱动核心源码](#6-tu5xx-驱动核心源码)
7. [测试程序与调试工具](#7-测试程序与调试工具)
8. [构建系统与依赖](#8-构建系统与依赖)
9. [部署与运行方式](#9-部署与运行方式)
10. [里程碑与演进路线](#10-里程碑与演进路线)
11. [调试方法论](#11-调试方法论)
12. [参考文档索引](#12-参考文档索引)

---

## 1. 项目概述

### 1.1 立项背景

针对**高通骁龙 625（MSM8953）+ Adreno 506 GPU** 移动平台（远程触屏移动终端，Ubuntu/Mobian 系）的 GPU 驱动与图形开发仓库。

**核心矛盾**：上游 Mesa Turnip Vulkan 驱动不支持 Adreno 506（实测报错 `tu_device.cc:651 device Turnip Adreno 506 unsupported`），而原生 GLES/EGL 路径（Freedreno FD506）可用（glmark2 200+ FPS）。项目为该设备自研 **tu5xx**（A5XX Vulkan 后端），打通 PC 交叉编译 → 设备部署 → 真机验证全链路。

### 1.2 核心目标

1. **GPU 图形链路落地**：原生 EGL/GLES（Mesa Freedreno）、Chromium 渲染、Vulkan 自研后端（tu5xx）。
2. **交叉编译与设备部署**：在 PC（Windows）上交叉编译 Mesa 及自研驱动并部署到设备。
3. **最终交付**：跑通真实端到端应用（非测试桩），交付级别。

### 1.3 关键术语

| 术语 | 含义 |
|---|---|
| tu5xx | 项目自研的 A5XX Vulkan 后端，基于 Mesa freedreno 改造 |
| fd5 / fd5 gallium | Mesa Gallium3D 的 Freedreno 驱动 A5XX 路径，作为参照基线 |
| Turnip | Mesa 上游 Vulkan Freedreno 驱动名（仅支持 A6XX+） |
| RD dump | Freedreno 命令流二进制转储（register dump），用于寄存器级对比 |
| sysmem / GMEM | sysmem=系统内存渲染路径；GMEM=Adreno 内置 tile mem（binning）路径 |
| mido | 红米 Note 4/4X 的设备代号 |

---

## 2. 整体架构

### 2.1 分层架构

```
┌──────────────────────────────────────────────────────────────────────┐
│  PC 侧（Windows d:\project\gpu）                                       │
│  ┌────────────┐  ┌──────────────┐  ┌────────────────┐  ┌──────────┐ │
│  │ 源码层       │  │ 交叉编译层     │  │ 工具链层        │  │ 文档层    │ │
│  │ patches/    │→ │ mesa-cross/  │  │ tools/ (ARM    │  │ *.md     │ │
│  │ work/mesa-  │  │ cross.ps1    │  │  toolchain15.2 │  │          │ │
│  │ src/        │  │ build-mesa   │  │  + MSYS2)      │  │          │ │
│  └────────────┘  └──────┬───────┘  └────────┬───────┘  └──────────┘ │
│                         │                    │                      │
│                  ┌──────┴────────────────────┘                      │
│                  ▼                                                    │
│  ┌─────────────────────────────┐  ┌────────────────┐                 │
│  │ sysroot/ (设备 arm64 镜像)    │  │ slib/ (junction)│                │
│  └─────────────────────────────┘  └────────────────┘                 │
└────────────────────────────────│─────────────────────────────────────┘
                                 │ plink / pscp（SSH 192.168.10.50）
                                 ▼
┌──────────────────────────────────────────────────────────────────────┐
│  设备侧（umeko@192.168.10.50）                                        │
│  ┌──────────┐   ┌─────────────┐   ┌──────────────┐   ┌───────────┐  │
│  │ Kernel    │   │ drm/msm +   │   │ Mesa         │   │ tu5xx      │  │
│  │ 6.7.5-rv2 │   │ Adreno 506  │   │ Freedreno    │   │ libvulkan_ │  │
│  │ umeko-    │   │             │   │ FD506 (GL)   │   │ freedreno  │  │
│  │ msm8953+  │   │             │   │              │   │ .so (自研)  │  │
│  └──────────┘   └─────────────┘   └──────────────┘   └───────────┘  │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  Xorg + openbox（图形会话）                                      │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 顶层目录结构

```
d:\project\gpu\
├── README.md                     项目概览（概览/约束/目录结构/里程碑/部署验证）
├── GPU_DEVELOPMENT.md            开发技术档案（降级命令 / 验证命令 / FAQ / 里程碑详情）
├── RECON_PHASE0.md               阶段 0 侦察报告（设备只读调研）
├── M1_KERN_BOOT.md               M1 内核启动相关说明
├── CODE_WIKI.md                  本文档
├── .gitignore
├── cross.ps1                     一键交叉编译封装（C/C++）
├── extract_sysroot.ps1           sysroot 解压脚本
├── record_com.ps1               串口日志记录工具
├── serial_probe.ps1             设备串口探测/自动登录
├── bootimg.py                   Android boot image v0 打包/解包/校验
├── unzstd.py                    zstandard 解压工具
├── gpu_audit.sh                  GPU 系统审计脚本
├── vkenum                        Vulkan 设备枚举器（已交叉编译产物）
├── mesa-cross/                   Mesa 交叉编译工作区
│   ├── build-mesa.ps1           Mesa 一键构建（setup/reconf/build）
│   ├── build-incremental.ps1    Mesa 增量编译
│   └── cross-aarch64.ini        Meson 交叉配置文件
├── patches/
│   └── mesa-24.0.5-tu5xx/       自研改动镜像快照（同步源，15 个文件）
│       └── src/freedreno/{common,registers,vulkan}/
├── work/                        工作区（源码 / 构建 / 测试 / 调试工具）
│   ├── mesa-src/mesa-24.0.5/    Mesa 24.0.5 完整源码（含 A5XX 特化）
│   ├── mesa-build/              Mesa 交叉编译产物（libvulkan_freedreno.so 等）
│   ├── vktriangle/              Vulkan 三角形测试桩 + 命令流解析工具
│   ├── *.py                     大量调试/比对/补丁工具
│   ├── *.sh                     设备端测试启动脚本（di*.sh 系列）
│   ├── glsmooth.c               最小 GLES2 smooth-varying 参照（fd5 RD 抓取）
│   ├── vkenum.c                 Vulkan 设备枚举器源码
│   ├── eglprobe.c / eglprobe2.c EGL 探针（X11 / surfaceless）
│   ├── deploy_tu5xx.ps1         tu5xx 驱动/测试程序部署脚本
│   ├── freedreno_icd.device.json Vulkan ICD 清单
│   ├── device_conf/             设备 dotfile 镜像（.profile/.xsessionrc 等）
│   ├── dumps/ dumps2/           RD dump 归档
│   ├── rdcap/                   RD 捕获工具/产物
│   ├── *.rd                     命令流 dump（00001.rd, 00002.rd, fault.rd 等）
│   └── *.png                    屏幕截图归档
├── tests/                       端到端验证桩（hello.c, hello.cpp）
├── tools/                       工具链（ARM GNU 15.2.Rel1 + MSYS2）
│   ├── bin/                     aarch64-none-linux-gnu-{gcc,g++,ar,...}.exe
│   ├── aarch64-none-linux-gnu/  工具链 sysroot
│   ├── msys64/                  MSYS2（提供 flex/bison/pkgconf 等 host 工具）
│   └── manifest.txt
├── sysroot/                     设备 Ubuntu 24.04 arm64 sysroot
│   ├── lib (junction → usr/lib)
│   └── usr/{include,lib,...}
├── slib (junction → sysroot/usr/lib/aarch64-linux-gnu)
├── device_conf/                 设备运行环境配置（profile/xinitrc/xsessionrc/zramswap）
├── linux-6.7.5/                 内核源码树（解压自 linux-6.7.5.tar.xz）
├── backup/                      灾备归档
│   ├── midobackup.tar.gz        设备完整备份
│   └── sysroot.tar.gz           sysroot 完整备份
└── tools_armgcc.{tar.xz,zip}    工具链压缩包归档
```

### 2.3 数据/控制流

```
[patches/mesa-24.0.5-tu5xx/]  ──同步──▶  [work/mesa-src/mesa-24.0.5/]
                                            │
                                            ▼
                          [mesa-cross/build-mesa.ps1]
                                            │（meson setup + ninja）
                                            ▼
                              [work/mesa-build/]
                                            │（libvulkan_freedreno.so）
                                            ▼
                          [work/deploy_tu5xx.ps1]  ──pscp──▶  设备 /home/umeko/tu5xx/
                                            │
                                            ▼
                          设备端 run*.sh  ──TU_DEBUG=rd──▶  *.rd dump
                                            │
                                            ▼
                                   pscp 拉回 work/
                                            │
                                            ▼
                          [work/cmp_fd5_tu.py 等比对工具] ──▶ 定位寄存器差异
                                            │
                                            ▼
                          修改 patches/ + 同步到 mesa-src → 重新交叉编译
```

---

## 3. 硬件与软件环境

### 3.1 设备硬件

| 项 | 值 |
|---|---|
| 设备代号 | mido（红米 Note 4/4X） |
| SoC | 高通骁龙 625（MSM8953） |
| GPU | Adreno 506（A5XX 家族） |
| GPU 能力 | OpenGL ES 3.x / EGL 可用；无 Vulkan 官方支持 |
| 内核 | 6.7.5-rv2-umeko-msm8953+（容器内交叉编译，aarch64-linux-gnu-gcc 9.4.0） |
| 操作系统 | Ubuntu 24.04 LTS (noble) |
| 存储 | eMMC 29.1G，A/B 布局，无 SD 卡 |
| 内存 | 2813 MB，无 swap |
| 图形会话 | Xorg + openbox |
| 显示 | MDP5 v1.16，panel xiaomi otm1911 |
| GPU 微码 | `a530_pm4.fw` / `a530_pfp.fw` 已加载；zap shader 未加载（无 zap 也能跑 GL） |

### 3.2 设备访问

- SSH：`umeko@192.168.10.50`，密码 `1234`
- 通道：PuTTY `plink.exe` / `pscp.exe`（`-ssh -batch -pw 1234`）
- 物理观察：仅支持目视观察触摸屏
- 串口：PC 端 COM35，波特率通常 115200（设备 UART 调试口，内核实验保命通道）
- 权限：umeko 已在 video/render 组（GPU 免 root），sudo 可用

### 3.3 当前驱动栈（已验证稳定）

| 层级 | 组件 | 状态 |
|---|---|---|
| 内核侧 | `msm` / `kgsl` DRM 驱动 | 正常，`/dev/dri` 节点正常 |
| 原生 GLES/EGL | Mesa Freedreno（FD506） | **可用**，`glmark2` 200+ FPS |
| Vulkan | **自研 tu5xx 后端** | M1.4 已像素级验证三角形渲染 |
| 2D 加速 | XRender / EXA | 可用 |
| Chromium 渲染 | SwiftShader 软渲染 | GPU 硬件加速无解（详见 §4） |

### 3.4 PC 工具链

| 组件 | 版本/路径 | 用途 |
|---|---|---|
| ARM GNU Toolchain | 15.2.Rel1（[tools/bin](tools/bin)） | aarch64-none-linux-gnu-gcc/g++/ar/nm/objcopy/objdump/readelf/strip/ld |
| MSYS2 | [tools/msys64](tools/msys64) | 提供 host 侧 flex/bison（GLSL 词法/语法分析器）、pkgconf |
| Meson + Ninja | MSYS2 ucrt64 | Mesa 构建系统 |
| PuTTY | `C:\Program Files\PuTTY` | plink/pscp 设备通信 |
| Python | 3.10+ | 调试/比对/补丁工具脚本 |
| zstandard | Python 包 | tools 解压 |

---

## 4. 关键约束与禁令

### 4.1 Mesa 版本雷区（最重要）

- **Mesa 必须锁 24.0.5**：26.1.7 与内核 6.7.5 不兼容（`DRM_MSM_GEM_INFO` 返回 -22 在 26.1.7 下为致命错误导致 GPU 进程崩溃，24.0.5 下仅为非致命警告）。
- **禁止升级 Mesa**。
- **禁止添加 kisak PPA**（会把系统拉回 26.x 的坑）。

降级命令（已执行，可复用）：
```bash
sudo apt-get install -y --allow-downgrades \
  libgl1-mesa-dri=24.0.5-1ubuntu1 libegl-mesa0=24.0.5-1ubuntu1 \
  libgbm1=24.0.5-1ubuntu1 libglx-mesa0=24.0.5-1ubuntu1 \
  libglapi-mesa=24.0.5-1ubuntu1 mesa-vulkan-drivers=24.0.5-1ubuntu1
```

### 4.2 Chromium 渲染定版

- **ANGLE 全后端不可用**：Chromium 151 Linux 仅 ANGLE 一个 GL 后端，ANGLE-GLES 在 msm_dri.so 触发 22 分钟死循环，drm-engine-gpu=0（铁证从未提交 GPU 命令）。
- **Chromium 必须带**（CPU 软渲染）：
  ```
  --disable-gpu-watchdog --disable-gpu-compositing --disable-background-networking --use-angle=swiftshader
  ```
- **Web 缩放用 `zoom`，禁用 `transform: scale()`**（软渲染下合成层会黑屏）。

### 4.3 内核侧已知问题

- 6.7.5-rv2 存在 `block_devnode+0x4` NULL deref Oops（fwupd 触发，开机 1 小时内 5 次，Tainted: D W）。
- `fb_set_par error -16 (EBUSY)` 反复出现（Xorg fbdev 交互），暂无影响。

### 4.4 工作约定（用户指令）

- **git 归档**：工作时间节点（任务完成、需求变更）都要 git commit 归档；每次小改动都归档。
- **一问一答**：不明确的就问用户，并说明为什么要问。
- **GitHub 上传**：每次解决或有了新进展时上传 GitHub 并更新说明。
- **设备设置**：红米 Note 4X Linux 设备需设定永不休眠和熄屏以便驱动测试（[work/00-nosleep.conf](work/00-nosleep.conf)）。

---

## 5. 主要模块职责

### 5.1 [mesa-cross/](mesa-cross) —— Mesa 交叉编译工作区

| 文件 | 职责 |
|---|---|
| [build-mesa.ps1](mesa-cross/build-mesa.ps1) | Mesa 一键构建：调用 `meson setup/configure` + `ninja`，禁用 EGL/GLX/GBM/OpenGL/GLES/视频编解码，仅构建 `gallium-drivers=freedreno` + `vulkan-drivers=freedreno` |
| [build-incremental.ps1](mesa-cross/build-incremental.ps1) | 增量编译：直接 `ninja -C $Bld`，复用已配置环境 |
| [cross-aarch64.ini](mesa-cross/cross-aarch64.ini) | Meson 交叉配置：x86_64 PC → aarch64 Linux，定义工具链二进制、sysroot、-isystem 多架构头、`-L slib`、release + b_staticpic |

### 5.2 [patches/mesa-24.0.5-tu5xx/](patches/mesa-24.0.5-tu5xx) —— 自研改动镜像快照

**作为同步源**，是项目核心代码层。所有自研 A5XX 特化改动以镜像方式存档于此（含 15 个文件），与 [work/mesa-src/mesa-24.0.5](work/mesa-src) 同步。详细源码说明见 [§6](#6-tu5xx-驱动核心源码)。

```
patches/mesa-24.0.5-tu5xx/src/freedreno/
├── common/
│   └── freedreno_gpu_event.h
├── registers/
│   └── gen_header.py            寄存器头生成脚本（A5XX 兼容性补丁）
└── vulkan/
    ├── meson.build              tu5xx 构建规则
    ├── tu_autotune.cc           性能自调优
    ├── tu_clear_blit.cc         clear/blit 操作（含 A5XX 特化）
    ├── tu_cmd_buffer.cc         命令缓冲区（核心，257KB）
    ├── tu_common.h
    ├── tu_device.cc             设备/物理设备（135KB）
    ├── tu_formats.cc / tu_formats.h  像素格式
    ├── tu_lrz.cc                LRZ（A5XX 跳过）
    ├── tu_pipeline.cc           管线构建（核心，202KB）
    ├── tu_pipeline.h
    ├── tu_shader.cc             着色器（核心，114KB）
    └── tu_util.h
```

### 5.3 [work/](work) —— 工作区（最大且最活跃）

| 子目录/文件 | 职责 |
|---|---|
| `mesa-src/mesa-24.0.5/` | Mesa 24.0.5 完整源码（含 A5XX 特化改动，从 patches 同步） |
| `mesa-build/` | Mesa 交叉编译产物（`libvulkan_freedreno.so` 等） |
| [vktriangle/](work/vktriangle) | Vulkan 三角形测试桩 + 命令流解析工具（详见 §7.1） |
| `*.rd` | RD 命令流 dump（`00001.rd`、`00002.rd`、`fault.rd`、`fresh_cur_00001.rd` 等） |
| `*.py` | 调试/比对/补丁工具（详见 §7.2） |
| `di*.sh` | 设备端诊断脚本系列（44 个，di.sh~di44.sh） |
| `run*.sh` | 测试启动脚本（`run_glsmooth.sh`、`run_tu_now.sh` 等） |
| [glsmooth.c](work/glsmooth.c) | 最小 GLES2 smooth-varying 三角形，用于抓取 fd5 RD 参照 |
| [vkenum.c](work/vkenum.c) | Vulkan 物理设备枚举器 |
| [eglprobe.c](work/eglprobe.c) / [eglprobe2.c](work/eglprobe2.c) | EGL 探针（X11 标准序列 / surfaceless/GBM/device） |
| [deploy_tu5xx.ps1](work/deploy_tu5xx.ps1) | tu5xx 驱动 + vkenum 部署脚本 |
| `freedreno_icd.device.json` | Vulkan ICD 清单 |
| `device_conf/` | 设备 dotfile 镜像（`.profile`、`.xsessionrc`、`zramswap`） |
| `dumps/` `dumps2/` | RD dump 归档 |
| `rdcap/` | RD 捕获工具/产物 |
| `*.png` | 屏幕截图归档（`scr_*.png`、`s*.png`、`t*.png` 等） |
| `00-nosleep.conf` | 设备永不休眠配置 |
| `config-6.7.5-rv2-umeko-msm8953` | 设备内核配置档 |

### 5.4 [tests/](tests) —— 端到端验证桩

仅含 [hello.c](tests/hello.c) 与 [hello.cpp](tests/hello.cpp)，用于验证 PC↔设备交叉编译环境打通（动态链 glibc 2.39 + 静态 libstdc++）。

### 5.5 [tools/](tools) —— 工具链

| 子目录 | 职责 |
|---|---|
| `bin/` | ARM GNU Toolchain 15.2.Rel1 可执行文件（aarch64-none-linux-gnu-{gcc,g++,ar,...}.exe） |
| `aarch64-none-linux-gnu/` | 工具链自带 sysroot |
| `msys64/` | MSYS2 环境（host 工具：flex/bison/pkgconf） |
| `include/` `lib/` `libexec/` `share/` `data/` | 工具链标准布局 |
| `manifest.txt` | 工具链清单 |
| `*.pkg.tar.{zst,}` | MSYS2 mingw 包（gcc-libs、pkgconf、zlib） |

### 5.6 [sysroot/](sysroot) + [slib](slib) —— 设备 sysroot

设备 Ubuntu 24.04 arm64 文件系统镜像，作为交叉编译链接阶段的根。

- [sysroot/usr/](sysroot/usr)：`include/aarch64-linux-gnu/`（多架构头）、`lib/aarch64-linux-gnu/`（库）、`share/pkgconfig/` 等
- [sysroot/lib](sysroot/lib)：junction → `usr/lib`（修复 ld 路径）
- [slib](slib)：junction → `sysroot/usr/lib/aarch64-linux-gnu`（cross.ps1 用的 -L 路径）

**sysroot 四项关键修复**（详见 [RECON_PHASE0.md](RECON_PHASE0.md) §8）：
1. `-isystem` 指多架构头文件目录
2. `crt*.o` 复制到 `usr/lib/`
3. 39 个 ld 脚本去绝对路径
4. `sysroot/lib` junction + `ld-linux` 副本 + `libc_nonshared.a` 单独从设备拉回

### 5.7 [device_conf/](device_conf) —— 设备运行环境配置

| 文件 | 职责 |
|---|---|
| `profile` | shell profile（环境变量） |
| `xinitrc` | X 启动脚本（启动 openbox 等） |
| `xsessionrc` | X 会话启动脚本 |
| `zramswap` | zram swap 配置（设备无 swap，重要） |

注：[work/device_conf/](work/device_conf) 是同一组文件的 dotfile 版本镜像。

### 5.8 [backup/](backup) —— 灾备归档

| 文件 | 大小 | 职责 |
|---|---|---|
| `midobackup.tar.gz` | ~107MB | 设备完整备份 |
| `sysroot.tar.gz` | ~515MB | sysroot 完整备份（extract_sysroot.ps1 解压源） |

### 5.9 [linux-6.7.5/](linux-6.7.5) —— 内核源码树

解压自 `work/linux-6.7.5.tar.xz`，标准 Linux 内核源码布局。设备内核构建方式为容器内交叉编译（aarch64-linux-gnu-gcc 9.4.0），有先例可循。

### 5.10 顶层独立脚本

| 文件 | 职责 |
|---|---|
| [cross.ps1](cross.ps1) | 通用 C/C++ 一键交叉编译封装（详见 §8.1） |
| [extract_sysroot.ps1](extract_sysroot.ps1) | 从 `backup/sysroot.tar.gz` 解压恢复 sysroot |
| [record_com.ps1](record_com.ps1) | 串口日志记录（指定 COM 端口，115200 波特率，追加到 `serial_log.txt`） |
| [serial_probe.ps1](serial_probe.ps1) | 串口探测 + 自动登录设备 |
| [bootimg.py](bootimg.py) | Android boot image v0 打包/解包/校验（解析 `ANDROID!` 头，提取 kernel/ramdisk/second/dt blob） |
| [unzstd.py](unzstd.py) | 用 zstandard 解压 `tools/` 下的 `.pkg.tar.zst` |
| [gpu_audit.sh](gpu_audit.sh) | GPU 系统审计 |
| `vkenum` | 已交叉编译的 [vkenum.c](work/vkenum.c) 产物 |

---

## 6. tu5xx 驱动核心源码

### 6.1 [tu_cmd_buffer.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_cmd_buffer.cc) —— 命令缓冲区（257KB，核心）

**职责**：Vulkan 命令缓冲区录制、render pass、draw/state 调度、sysmem/GMEM 路径切换。

**关键函数**：
- `tu_emit_cache_flush_ccu<A5XX>`：A5XX 特化，跳过空 `RB_CCU_CNTL` reg 对避免 CP 命流失步（A5XX 无 CCU，空 reg 对产生悬空 pkt4 包头，CP 把下一包头当数据 → opcode error 根因，M1.3 修复 commit d0b7619）
- `sysmem_render_begin<A5XX>`：sysmem 渲染路径开始，发 `VPC_SO_OVERRIDE`（先发 SO_DISABLE 再清 0，匹配 fd5 restore + sysmem_prep）
- `tu6_init_hw<A5XX>`：移植 `fd5_emit_restore` 完整寄存器包，真 GPU 执行（M1.2 commit 3232ef7）
- `tu_draw_initiator()`：发 `VIS_CULL(USE_VISIBILITY)` 生成 `CP_SET_VISIBILITY_OVERRIDE(1)` opcode，A5XX 需条件化处理
- `tu6_emit_window_scissor`：三芯片（A5XX/A6XX/A7XX）显式实例化（跨 TU 链接）

### 6.2 [tu_pipeline.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_pipeline.cc) —— 管线构建（202KB，核心）

**职责**：Vulkan graphics/compute pipeline 状态编译，把 pipeline state 翻译为 GPU 寄存器序列。

**A5XX 关键修复**（M1.4 commit 27a4c8a）：
- `GRAS_SC_VIEWPORT_SCISSOR_TL/BR_0`：补发（漏发 → 复位默认 0，裁掉整个视口，零覆盖主因；fd5 在 fd5_emit.c:618 与 `GRAS_SC_SCREEN_SCISSOR` 配对）
- `RB_FS_OUTPUT_CNTL` / `SP_FS_OUTPUT_CNTL`：补 MRT 路由
- `RB_BLEND_CNTL` SAMPLE_MASK：补发（原值 0 屏蔽所有样本）
- `TPL1_TP_RAS_CNTL` / `TPL1_TP_DEST_MSAA_CNTL`：补发复位默认值
- `tu5_emit_vpc`：移植 fd5 VPC 连线（VPC_VAR_DISABLE/SP_VS_OUT/VPC_DST/INTERP_MODE/PS_REPL/CLIP_CNTL），返回 max_loc 存 `program->vpc_stride`
- `tu5_emit_program_config`：补 VFD_CONTROL_1..5（VERTEX_ID_ZERO_BASE/INSTANCE_ID regid）
- `tu6_draw_common<A5XX>`：发 `PC_PRIMITIVE_CNTL`（STRIDE/PRIMITIVE_RESTART/PROVOKING_VTX_LAST），跳过 tess 与 LRZ
- `tu6_emit_vs_params`：A5XX 走 `VFD_INDEX_OFFSET` pkt4 + `CP_LOAD_STATE4`（ST4_CONSTANTS, SB4_VS_SHADER）

### 6.3 [tu_shader.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_shader.cc) —— 着色器（114KB，核心）

**职责**：SPIR-V → ir3 着色器编译、VS/FS 寄存器发射。

**A5XX 特化**（M1.3 commit 6a333ae）：
- `tu6_emit_fs<A5XX>`：inputs early-return 由 program_config 覆盖；outputs 只发 `A5XX_RB_RENDER_COMPONENTS`；跳过 `VFD_CONTROL_6`/`PC_PS_CNTL`
- `tu6_emit_fs_inputs<A5XX>`：early return 跳过 a6xx 独占寄存器（`SP_FS_PREFETCH_CNTL`、`VPC_VAR_DISABLE`、`VPC_CNTL_0`、`VFD_CONTROL_6`、`PC_PS_CNTL`），仅发 `A5XX_RB_RENDER_COMPONENTS` 防止寄存器解析错
- `tu6_emit_fs_outputs<A5XX>`：仅发 `A5XX_RB_RENDER_COMPONENTS`
- `SP_FS_CTRL_REG0.VARYING`：`COND(total_in > 0)` 设置（控制 FS 访问 per-primitive varying 存储），匹配 fd5 语义（H2c commit 72c1b38）
- `VPC_VARYING_INTERP_MODE`：全 0（smooth linear 插值 via bary path），匹配 fd5（commit f0da08f）
- `tu5_emit_xs`：修 `PVT_MEM_SIZE`（A5XX 无 PERWAVEMEMLAYOUT）

### 6.4 [tu_device.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_device.cc) —— 设备/物理设备（135KB）

**职责**：VkInstance/VkPhysicalDevice/VkDevice 实现、特性/属性查询、内存类型、队列。

### 6.5 [tu_clear_blit.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_clear_blit.cc) —— clear/blit（148KB）

**职责**：clear/blit 操作，含 r3d blit engine（sysmem load/store）、`tu6_emit_mrt<A5XX>`、`tu_CmdFillBuffer<A5XX>` 特化、`tu5_format_color` 等。

### 6.6 其他 vulkan/ 文件

| 文件 | 行数 | 职责 |
|---|---|---|
| [tu_autotune.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_autotune.cc) | 22KB | 性能自调优（GMEM vs sysmem 决策） |
| [tu_lrz.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_lrz.cc) | 27KB | LRZ（A5XX 跳过） |
| [tu_formats.cc](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_formats.cc) | 35KB | 像素格式映射（含 B10G10R10A2 等） |
| [tu_common.h](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_common.h) | 7KB | 公共定义 |
| [tu_util.h](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_util.h) | 14KB | 工具宏/内联 |
| [tu_pipeline.h](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_pipeline.h) | 7KB | 管线结构体定义 |
| [tu_formats.h](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/tu_formats.h) | 1.5KB | 格式表声明 |
| [meson.build](patches/mesa-24.0.5-tu5xx/src/freedreno/vulkan/meson.build) | 5.5KB | tu5xx 构建规则 |

### 6.7 寄存器层与公共层

| 文件 | 职责 |
|---|---|
| [registers/gen_header.py](patches/mesa-24.0.5-tu5xx/src/freedreno/registers/gen_header.py) | 寄存器头生成脚本；补 fallback return（reg==0 被 tu_cs_emit_regs 丢弃，M1.2 commit 89ac820） |
| [common/freedreno_gpu_event.h](patches/mesa-24.0.5-tu5xx/src/freedreno/common/freedreno_gpu_event.h) | GPU 事件定义 |

### 6.8 参照基线：fd5 gallium

fd5 是 Mesa Gallium3D 的 Freedreno A5XX 路径，是 GL 工作基线，作为 tu5xx 调试的"金参考"。源码位于 [work/mesa-src/mesa-24.0.5/src/gallium/drivers/freedreno/a5xx/](work/mesa-src)。关键参照文件：
- `fd5_emit.c`：寄存器发射（`fd5_emit.c:618` 是 `GRAS_SC_VIEWPORT_SCISSOR` 与 `SCREEN_SCISSOR` 配对处）
- `fd5_program.c`：着色器程序配置（`SP_FS_CTRL_REG0.VARYING` 设置依据）
- `fd5_gmem.c`：GMEM/binning 路径（`VPC_SO_OVERRIDE` sysmem_prep 清 0 在 L769-771）

---

## 7. 测试程序与调试工具

### 7.1 [work/vktriangle/](work/vktriangle) —— Vulkan 测试桩 + 解析工具

| 文件 | 职责 |
|---|---|
| [vktriangle.c](work/vktriangle/vktriangle.c) | M1.3 探针：renderpass + pipeline + draw 全套（21KB）。loadOp/storeOp=DONT_CARE，静态 viewport/scissor，无 push consts/descriptors |
| [vktriangle_flat_red.c](work/vktriangle/vktriangle_flat_red.c) | flat 纯红对照实验（H2b 选项 B） |
| `vktriangle` / `vktriangle_flat_red` | 已交叉编译产物 |
| [spirv_tri.h](work/vktriangle/spirv_tri.h) | 内嵌 SPIR-V 字节码（多彩 R/G/B 三顶点） |
| [spirv_tri_flat_red.h](work/vktriangle/spirv_tri_flat_red.h) | flat 红色 SPIR-V |
| [gen_spirv.py](work/vktriangle/gen_spirv.py) | SPIR-V 生成器 |
| [gen_spirv_c792f37.py](work/vktriangle/gen_spirv_c792f37.py) | 针对特定 commit 的 SPIR-V 生成 |
| [gen_spirv_flat_red.py](work/vktriangle/gen_spirv_flat_red.py) | flat 红 SPIR-V 生成 |
| `run*.sh` | 设备端测试启动脚本（run6.sh~run17.sh，run_fs.sh 等） |
| [run15.sh](work/vktriangle/run15.sh) | 抓 RD dump 验证像素读回（`TU_DEBUG=sysmem,rd`） |
| [run_fs.sh](work/vktriangle/run_fs.sh) | 抓 FS/VS ir3 反汇编 + readback |
| `*.rd` | RD dump（`fresh_cur_00001.rd`、`multicolor_00001.rd`、`flat_red_00001.rd` 等） |
| `*.dis` | ir3 反汇编（`run15_00001.dis` 等） |
| [parse_devcd.py](work/vktriangle/parse_devcd.py) | devcoredump 解析 |
| [devcd*.txt](work/vktriangle/devcd.txt) | devcoredump 文本归档 |
| [decode_groups.py](work/vktriangle/decode_groups.py) | 寄存器组解码 |
| [scan_shift.py](work/vktriangle/scan_shift.py) | 寄存器位扫描 |
| [check_regs.py](work/vktriangle/check_regs.py) | 寄存器存在性检查 |
| `vk_hdr_repo/` `vk_inc/` | Vulkan 头文件依赖 |
| `find_bad.py` | 异常寄存器定位 |

### 7.2 [work/](work) 调试/比对/补丁工具

**寄存器对比工具**（核心）：

| 工具 | 职责 |
|---|---|
| [cmp_fd5_tu.py](work/cmp_fd5_tu.py) | fd5 vs tu5xx draw IB2 寄存器序列对比；输出 fd5 写了但 tu 没写、两边都写但值不同的寄存器；从 a5xx.xml 解析寄存器名 |
| [cmp_fd5_varying.py](work/cmp_fd5_varying.py) | 针对 fd5(GL,工作) vs tu(fresh_varying,坏) 的 varying 递送寄存器逐项对比 |
| [cmp_fd5_multi.py](work/cmp_fd5_multi.py) | 多组对比 |
| [cmp_flat_vs_multi.py](work/cmp_flat_vs_multi.py) | flat 红色 vs 多彩对比 |
| [cmp_full.py](work/cmp_full.py) | 全量对比 |
| [cmp_varying_diff.py](work/cmp_varying_diff.py) | varying 差异 |
| [map_regs.py](work/map_regs.py) | 将 RD dump 反汇编中的寄存器映射为 a5xx 寄存器名并统计缺失 |
| [parse_rd.py](work/parse_rd.py) | RD dump 解析器 |
| [rd_census.py](work/rd_census.py) | RD 命令普查 |
| [rd_sections.py](work/rd_sections.py) | RD 段落解析 |
| [scan_rd.py](work/scan_rd.py) / [scan_cands.py](work/scan_cands.py) | RD 扫描/候选定位 |

**SPIR-V 工具**：

| 工具 | 职责 |
|---|---|
| [parse_spirv.py](work/parse_spirv.py) / [parse_spirv_fs.py](work/parse_spirv_fs.py) / [analyze_spirv.py](work/analyze_spirv.py) | SPIR-V 解析 |
| [edit_spirv.py](work/edit_spirv.py) | SPIR-V 编辑 |
| [modify_spirv.py](work/modify_spirv.py) / [modify_spirv_vec2.py](work/modify_spirv_vec2.py) | SPIR-V 修改 |
| [handcraft_spirv.py](work/handcraft_spirv.py) | SPIR-V 手工构造 |
| [fix_spirv_bounds.py](work/fix_spirv_bounds.py) / [fix_c792f37_bounds.py](work/fix_c792f37_bounds.py) | SPIR-V 边界修复 |

**反汇编/解码工具**：

| 工具 | 职责 |
|---|---|
| [fault_dis.py](work/fault_dis.py) | fault RD 反汇编 |
| [dis_sub.py](work/dis_sub.py) | 子程序反汇编 |
| [decode_3regs.py](work/decode_3regs.py) | 3 寄存器解码 |
| [debug_instrs.py](work/debug_instrs.py) / [debug_words.py](work/debug_words.py) | 指令/字调试 |
| [full_decode.py](work/full_decode.py) | 全量解码 |
| [proper_parser.py](work/proper_parser.py) | 正式解析器 |
| [dmp_analyze.py](work/dmp_analyze.py) | dump 分析 |

**BO/绑定检查**：

| 工具 | 职责 |
|---|---|
| [check_bo.py](work/check_bo.py) / [cmp_bo.py](work/cmp_bo.py) / [cmp_bo2.py](work/cmp_bo2.py) | BO 检查/对比 |
| [check_bound.py](work/check_bound.py) / [check_my_bound.py](work/check_my_bound.py) / [check_max_id.py](work/check_max_id.py) / [check_ids.py](work/check_ids.py) | 绑定检查 |
| [check_types.py](work/check_types.py) | 类型检查 |
| [check_shader.py](work/check_shader.py) / [check_all.py](work/check_all.py) | 着色器/全检查 |
| [probe_varying.py](work/probe_varying.py) | varying 探测 |

**补丁工具**：

| 工具 | 职责 |
|---|---|
| [direct_patch.py](work/direct_patch.py) / [inplace_patch.py](work/inplace_patch.py) | 直接/原地补丁 |
| [simple_patch.py](work/simple_patch.py) / [patch_minimal.py](work/patch_minimal.py) | 简单/最小补丁 |
| [proper_patch.py](work/proper_patch.py) | 正式补丁 |
| [patch_exact_working.py](work/patch_exact_working.py) | 精确工作补丁 |
| [patch_flat_red_minimal.py](work/patch_flat_red_minimal.py) / [patch_flat_red_spirv.py](work/patch_flat_red_spirv.py) | flat 红补丁 |
| [match_flat_red.py](work/match_flat_red.py) / [scan_flat_red.py](work/scan_flat_red.py) | flat 红匹配/扫描 |
| [fix_string.py](work/fix_string.py) | 字符串修复 |

**设备端脚本**：

| 脚本 | 职责 |
|---|---|
| [fd5_capture.sh](work/fd5_capture.sh) | 抓取 fd5 gallium 干净 RD 参照（`FD_MESA_DEBUG=rd` + glmark2） |
| [run_glsmooth.sh](work/run_glsmooth.sh) | 启动 glsmooth 参照程序 + 捕获最小 fd5 GL smooth-varying RD |
| [run_glsmooth_disasm.sh](work/run_glsmooth_disasm.sh) | glsmooth 反汇编 |
| [run_tu_now.sh](work/run_tu_now.sh) | 当前 tu5xx 运行 |
| [check_fd5.sh](work/check_fd5.sh) | 确认 fd5 是否产生 00002.rd |
| [chk_gallium_rd.sh](work/chk_gallium_rd.sh) | gallium RD 检查 |
| [find_rd.sh](work/find_rd.sh) | RD 定位 |
| [dbg_check.sh](work/dbg_check.sh) / [dbg_check2.sh](work/dbg_check2.sh) | 调试检查 |
| [guide_check.sh](work/guide_check.sh) ~ [guide_check3.sh](work/guide_check3.sh) | 向导检查 |
| [perf_apply.sh](work/perf_apply.sh) / [perf_apply2.sh](work/perf_apply2.sh) / [perf_audit.sh](work/perf_audit.sh) / [perf_install2.sh](work/perf_install2.sh) | 性能调优 |
| [vulkan_probe.sh](work/vulkan_probe.sh) | Vulkan 探测 |
| [venus_*.sh](work/venus_diag.sh) | Venus 视频解码测试（8 个） |
| [encode_src.sh](work/encode_src.sh) | 源编码 |
| [cpu-governor.service](work/cpu-governor.service) / [check_gov.sh](work/check_gov.sh) | CPU governor |
| [openbox-autostart](work/openbox-autostart) | openbox 自启 |
| `di*.sh` | 44 个设备诊断脚本（di.sh~di44.sh，多用于驱动调试的不同场景） |

### 7.3 参照程序

| 程序 | 职责 |
|---|---|
| [glsmooth.c](work/glsmooth.c) | 最小 GLES2 smooth color-varying 三角形；H2b 用于捕获 CLEAN per-draw FS-varying fd5 RD 参照；ESL surfaceless + pbuffer 128x128；VS 输出 vec3 smooth varying vColor，FS 直接读取 → gl_FragColor；全部 GL/EGL 符号 via dlopen/dlsym，无头依赖，可交叉编译 |
| [vkenum.c](work/vkenum.c) | Vulkan 物理设备枚举器；用真实 Vulkan 头文件（Mesa include/vulkan），via dlopen；M1.1 用于验证 tu5xx 设备枚举打通 |
| [eglprobe.c](work/eglprobe.c) | X11 标准 EGL 序列探针（Initialize→ChooseConfig→ES3 Context→MakeCurrent） |
| [eglprobe2.c](work/eglprobe2.c) | surfaceless/GBM/device EGL 探针 |

---

## 8. 构建系统与依赖

### 8.1 [cross.ps1](cross.ps1) —— 通用 C/C++ 一键交叉编译封装

**用法**：
```powershell
.\cross.ps1 tests\hello.c                              # C, 输出 hello
.\cross.ps1 tests\hello.cpp -Cpp                       # C++ (静态 libstdc++)
.\cross.ps1 src\foo.c -o out\foo -O2 -lEGL             # 透传任意编译参数
```

**工作流程**（[cross.ps1](cross.ps1)）：
1. 设 `$Root = d:\project\gpu`、`$S = sysroot`、`$I = sysroot/usr/include/aarch64-linux-gnu`、`$Bin = tools/bin`
2. **自动重建 sysroot 关键修复**（junction 打包/复制后会失效）：
   - 若 `slib` 不存在，创建 junction → `sysroot/usr/lib/aarch64-linux-gnu`
   - 若 `sysroot/lib` 不存在，创建 junction → `sysroot/usr/lib`
   - 复制 `crt1.o/Scrt1.o/crti.o/crtn.o/Mcrt1.o/ld-linux-aarch64.so.1` 到 `sysroot/usr/lib/`
3. 构造 argv：`--sysroot=$S -isystem $I -L $Root/slib -I $Root/work/mesa-src/mesa-24.0.5/include`
4. C 用 `aarch64-none-linux-gnu-gcc.exe`，C++ 用 `aarch64-none-linux-gnu-g++.exe -static-libstdc++`
5. 透传剩余参数

### 8.2 Mesa 构建流程

**入口**：[mesa-cross/build-mesa.ps1](mesa-cross/build-mesa.ps1)

```powershell
powershell -ExecutionPolicy Bypass -File mesa-cross\build-mesa.ps1 [setup|reconf|build]
```

**环境变量**：
- `PKG_CONFIG_LIBDIR` = sysroot pkgconfig 路径
- 去掉 `PKG_CONFIG_SYSROOT_DIR`（靠编译器 --sysroot 自动解析，避免双前缀）
- `PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1`
- `LIBRARY_PATH` = sysroot 多架构库目录
- `Path` 前置 MSYS2（flex/bison/pkgconf）

**Meson 选项**：
```
--cross-file=mesa-cross/cross-aarch64.ini
-Dplatforms=             (禁所有 window system)
-Degl=disabled -Dglx=disabled -Dgbm=disabled
-Dgallium-drivers=freedreno
-Dvulkan-drivers=freedreno
-Dllvm=disabled -Dvideo-codecs=
-Dopengl=false -Dgles1=disabled -Dgles2=disabled
-Dbuild-tests=false
--wrap-mode=nodownload
```

**三个动作**：
- `setup`：`meson setup $Bld $Src @opts`
- `reconf`：`meson configure $opts $Bld`
- `build`：`ninja -C $Bld`

**增量编译**：[mesa-cross/build-incremental.ps1](mesa-cross/build-incremental.ps1) 直接 `ninja -C $Bld`

### 8.3 [cross-aarch64.ini](mesa-cross/cross-aarch64.ini) —— Meson 交叉配置

- `[binaries]`：所有 aarch64-none-linux-gnu-{gcc,g++,ar,nm,objcopy,objdump,readelf,strip,ld}.exe + pkgconf.exe
- `[host_machine]`：linux / aarch64 / little
- `[properties]`：
  - `sys_root = D:/project/gpu/sysroot`
  - `c_args`/`cpp_args`：`--sysroot` + `-isystem`（多架构头 + libdrm + drm）+ `-LD:/project/gpu/slib` + `-fPIC`
  - `c_link_args`/`cpp_link_args`：`--sysroot` + `-LD:/project/gpu/slib`
- `[built-in options]`：`buildtype=release`、`b_staticpic=true`

### 8.4 关键依赖关系

| 依赖 | 版本 | 角色 |
|---|---|---|
| Mesa | 24.0.5（上游） | 基础框架，tu5xx 嵌入其 `src/freedreno/vulkan/` |
| libdrm | libdrm-freedreno1（设备已装） | DRM 接口 |
| glibc | 2.39（设备 Ubuntu 24.04） | 动态链接 |
| libstdc++ | static（C++ 默认 `-static-libstdc++`） | C++ 运行时 |
| ir3 | Mesa 内置 | Adreno 着色器编译器（SPIR-V → ir3 → 机器码） |
| a5xx.xml | Mesa 内置（[work/mesa-src/mesa-24.0.5/src/freedreno/registers/adreno/a5xx.xml](work/mesa-src)） | A5XX 寄存器定义（cmp_fd5_tu.py 解析源） |
| flex/bison | MSYS2 | GLSL 词法/语法分析器（host 侧） |
| pkgconf | MSYS2 ucrt64 | pkg-config 替代 |
| PuTTY plink/pscp | 系统安装 | 设备通信 |

### 8.5 sysroot 维护

- **重建**：[extract_sysroot.ps1](extract_sysroot.ps1) 删除旧 `sysroot/`，从 `backup/sysroot.tar.gz` 解压
- **修复**：cross.ps1 启动时自动重建 junction 与 crt 文件复制
- **未版本化 .so 计数**：extract_sysroot.ps1 记录数量以验证完整性

---

## 9. 部署与运行方式

### 9.1 [work/deploy_tu5xx.ps1](work/deploy_tu5xx.ps1) —— 驱动部署

**流程**：
1. `plink` 在设备 `umeko@192.168.10.50` 创建 `/home/umeko/tu5xx`
2. `pscp` 上传 `work/mesa-build/src/freedreno/vulkan/libvulkan_freedreno.so`
3. `pscp` 上传 `work/freedreno_icd.device.json`（ICD 清单）
4. `pscp` 上传 `work/vkenum`（枚举器）
5. `plink` chmod +x 后用 `VK_ICD_FILENAMES` 运行 vkenum 验证

### 9.2 设备端运行 vktriangle

**典型流程**（参考 [run15.sh](work/vktriangle/run15.sh)）：
```sh
cd /home/umeko
echo 1234 | sudo -S dmesg -C                                     # 清 dmesg
rm -rf /home/umeko/.cache/mesa_shader_cache*                     # 清着色器缓存
rm -f /home/umeko/*.rd                                           # 清旧 RD
timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json \
    TU_DEBUG=sysmem,rd TU5XX_TRACE=1 MESA_SHADER_CACHE_DISABLE=true \
    ./tu5xx/vktriangle > /tmp/run15.log 2>&1
echo "exit=$?"
grep -E 'readback|verify|fill buffer|clean exit|pixel' /tmp/run15.log
dmesg | grep -E 'fault|opcode|protected|hangcheck' | tail -6
ls -la /home/umeko/*.rd
```

**关键环境变量**：
- `VK_ICD_FILENAMES`：指向 tu5xx ICD json
- `TU_DEBUG=sysmem,rd`：sysmem 路径 + RD dump
- `TU5XX_TRACE=1`：tu5xx 跟踪
- `MESA_SHADER_CACHE_DISABLE=true`：禁用着色器缓存（保证 dump 一致性）
- `FD_MESA_DEBUG=rd`：fd5 gallium RD 抓取（用于参照）

### 9.3 PC 端拉回 RD dump

```powershell
& "C:\Program Files\PuTTY\pscp.exe" -ssh -batch -pw 1234 `
    umeko@192.168.10.50:/home/umeko/00001.rd d:\project\gpu\work\vktriangle\fresh_cur_00001.rd
```

### 9.4 设备永不休眠配置

[work/00-nosleep.conf](work/00-nosleep.conf)：部署到设备 systemd 配置目录，防止触屏熄屏打断驱动测试。

### 9.5 ICD 清单 [work/freedreno_icd.device.json](work/freedreno_icd.device.json)

Vulkan ICD 清单文件，告诉加载器去哪找 `libvulkan_freedreno.so`。

---

## 10. 里程碑与演进路线

### 10.1 已完成里程碑

| 里程碑 | 内容 | 关键 commit |
|---|---|---|
| **M0** | PC↔设备交叉编译环境打通（MSYS2 + ARM 官方工具链 + 设备 sysroot） | - |
| **M1.0** | Mesa 24.0.5 交叉编译打通 | - |
| **M1.1** | A506 物理设备枚举打通（vkenum） | - |
| **M1.2 产出一** | A5XX 模板 variant 编入 turnip .so，设备空提交全绿；`tu6_init_hw`/`render_cntl` a5xx stub；gen_header.py fallback return；源码快照首次入 git | `89ac820` |
| **M1.2 产出二** | 非空命令缓冲录制/提交真机打通；tu5xx 命令流骨架端到端全通 | `f55b913` |
| **M1.2 产出三** | `tu6_init_hw<A5XX>` 移植 `fd5_emit_restore` 完整寄存器包，真 GPU 执行（非空 CB 提交 fence OK 无 hang）；a5xx.xml.h 经 namespace 隔离引入解决与 a6xx unscoped enum 冲突 | `3232ef7` |
| **M1.3 产出一** | A5XX 渲染路径补齐，libvulkan_freedreno.so 交叉编译链接通过；`tu5_emit_vpc`、`tu5_emit_program_config`、`tu6_draw_common<A5XX>`、`tu6_emit_vs_params` 模板化、`tu5_emit_xs` 修 PVT_MEM_SIZE、`tu5_format_vtx`、`prim_order` A5XX 等 | `02eb911` |
| **M1.3 缺陷修复** | `tu_emit_cache_flush_ccu<A5XX>` 跳过空 RB_CCU_CNTL reg 对避免 CP 命流失步（opcode error 根因） | `d0b7619` |
| **M1.3 产出二** | A506 真机第一个 vkCmdDraw 三角形打通；`tu6_emit_fs` A5XX 特化；修复 4 类硬伤（CCU 空 reg 对失步 / a6xx 寄存器泄漏 / draw state 组表越界 LOAD_IMMED / FS 组 HLSQ_CONTROL reg=0）；dmesg 清空后连续两次 vktriangle 全绿 | `6a333ae` |
| **M1.4** | A506 tu5xx 三角形**像素读回验证通过**（center=RGBA(0,255,0,255), green=20808, clean exit, dmesg 0 行）；对比 fd5 命令流定位 4 类缺失状态（GRAS_SC_VIEWPORT_SCISSOR_TL/BR_0 漏发为主因、RB_FS_OUTPUT_CNTL/SP_FS_OUTPUT_CNTL 的 MRT 路由、RB_BLEND_CNTL SAMPLE_MASK、TPL1_TP_RAS/DEST_MSAA_CNTL）；cmp_fd5_tu.py/map_regs.py 入库 | `27a4c8a` |

### 10.2 进行中：M1.5 / H2 —— VS→FS 多彩 varying 插值递送调查

| 子项 | 内容 | commit |
|---|---|---|
| H2a | 寄存器级对比无缺失（结论性） | `7dfecab` |
| H2c | VPC/SP 关键位对齐 fd5（SP_FS_CTRL_REG0.VARYING、VPC_VARYING_INTERP_MODE），非根因 | `72c1b38` |
| H2b 选项 3 | varying 改 slot32(VAR0)+平滑插值并重编重部署；readback 三角形光栅化 OK（nonbg=15606, center=RGBA(0,1,0,255)）但三顶点 probe 全 MISMATCH — varying 仍显 flat 单一绿色 | `f0da08f` |
| H2b 决定性对比 | 工作 GL smooth 用 `bary.f`（硬件插值）读 varying；tu multicolor 用 `ldlv l[0..2]` 直读读 0，平滑 bary 也试过为 0；根因收敛为 VPC/SP 未把 VS 颜色写进 FS 可读 varying 存储 | `79231df` |
| glsmooth.c | 最小 fd5 GL smooth-varying 参照（可跑通渲染三角，X11）；确认 mesa-24.0.5 gallium 无 RD dump | `3fe2ed3` |
| 工具修正 | RD type 语义（redump.h）& DRAW opcode 0x38；新增 cmp_fd5_varying；scan_rd 更新 | `9a44f36` |

**当前阻塞项**：FS 侧 varying 读 0 问题。VPC/program 命令流已 FULLY fd5-identical，根因不在任何已 diff 的 VPC/SP 寄存器。开放假设 H2b 选项 B：tu5xx FS varying-storage base / draw-path delivery —— FS 如何读 per-primitive varying 存储（`ldlv l[]` 读 0；bary variant 也读 0 → varying 从未到达 FS）。需调研 FS local/param base、SP_IBO_BASE、per-draw varying buffer base、PC_PRIMITIVE_CNTL STRIDE，或 ir3 LDG/LDLV base 形成。

### 10.3 遗留问题（来自 M1.4）

- GMEM/binning 路径实现
- COLOR_SWAP 字节序（fd5 用 WZYX）
- 未用 MRT 槽清零
- `gl_FragDepth` 的 DEPTH_REGID 接线
- `tu6_emit_fs_inputs` A5XX-specialized 仍需注意 a6xx 独占寄存器（SP_FS_PREFETCH_CNTL/VPC_VAR_DISABLE/VPC_CNTL_0/VFD_CONTROL_6/PC_PS_CNTL）

### 10.4 Lessons Learned

- `tu_emit_cache_flush_ccu` 在 A5XX 用空寄存器对发 pkt4 包头，导致命令流失步和 CP opcode error (0x480B7A01) — 修复：A5XX 跳过空寄存器写
- vktriangle GPU fault（hangcheck recover）源于未打补丁驱动 + CCU 空包头 + a6xx 寄存器泄漏 — 需部署 PC 侧修复并交叉编译的驱动
- `tu6_emit_fs` 必须 A5XX 特化：跳过 a6xx 独占寄存器，仅发 `A5XX_RB_RENDER_COMPONENTS`
- `tu_cs_emit_regs` 实现空寄存器对留悬空 pkt4 包头（先发包头，__ONE_REG 仅跳过值）
- `tu_draw_initiator()` 无条件设 `VIS_CULL(USE_VISIBILITY)`，可能生成 A5XX firmware 不存在的 `CP_SET_VISIBILITY_OVERRIDE(1)` opcode
- A5XX 零覆盖根因（M1.4 已修）：`GRAS_SC_VIEWPORT_SCISSOR_TL/BR_0` 从未发射，复位 0x0 裁剪整个视口
- A5XX 状态差异最佳调试法：捕获 fd5 gallium RD dump（`FD_MESA_DEBUG=rd`）作为参照，与 tu5xx dump 逐寄存器 diff（`work/cmp_fd5_tu.py`）；多数 RB_MRT/VFD/CONFIG 差异是 GMEM-vs-sysmem 或顶点格式差异，聚焦 fd5 写而 tu5xx 未写的寄存器
- `VPC_SO_OVERRIDE` 已确认 INERT（M1.4）：不要为黑屏问题再次探索此寄存器
- glsmooth（`work/glsmooth.c`）是 WORKING fd5 GL 单 varying 参照：device center=128,0,128 err=0；FS 通过 `bary.f`（硬件插值）读颜色
- `SP_FS_CTRL_REG0.VARYING` 必须在 FS 有 varying 输入（total_in > 0）时设置 — 控制 FS 访问 per-primitive varying 存储
- `VPC_VARYING_INTERP_MODE` 必须全 0（平滑线性插值 via bary path）当无 flat 插值时 — 匹配 fd5

---

## 11. 调试方法论

### 11.1 寄存器级对比法（核心方法）

**目标**：以工作的 fd5 gallium GL 命令流为金参考，与 tu5xx Vulkan 命令流逐寄存器 diff，定位 tu5xx 漏发/错发的状态。

**流程**：
1. 设备端运行 fd5 程序（如 glmark2、glsmooth）带 `FD_MESA_DEBUG=rd`，捕获 `*.rd`
2. 设备端运行 tu5xx 测试（如 vktriangle）带 `TU_DEBUG=sysmem,rd`，捕获 `*.rd`
3. PC 端用 [cmp_fd5_tu.py](work/cmp_fd5_tu.py) diff：聚焦 fd5 写了但 tu 没写的寄存器、两边都写但值不同的寄存器
4. 用 [map_regs.py](work/map_regs.py) 将反汇编寄存器地址映射为 a5xx.xml 中的寄存器名
5. 对照 fd5 源码（`fd5_emit.c`、`fd5_program.c`、`fd5_gmem.c`）确认语义
6. 修改 tu5xx 对应函数，重新交叉编译部署

### 11.2 devcoredump 解析

设备 GPU fault 时内核生成 devcoredump。用 [parse_devcd.py](work/vktriangle/parse_devcd.py) 解析 [devcd*.txt](work/vktriangle/devcd.txt)，提取 fault 时寄存器状态、命令流片段。

### 11.3 ir3 反汇编

RD dump 含 ir3 着色器字节码。用 Mesa 的 ir3 反汇编工具（如 `run15_00001.dis` 系列）查看 VS/FS 实际指令，验证 `ldlv`/`bary.f` 等 varying 读取路径。

### 11.4 关键调试环境变量

| 变量 | 作用 |
|---|---|
| `TU_DEBUG=sysmem` | 强制 sysmem 路径（避开 GMEM/binning 复杂度） |
| `TU_DEBUG=rd` | 输出 RD dump |
| `TU5XX_TRACE=1` | tu5xx 自定义跟踪 |
| `FD_MESA_DEBUG=rd` | fd5 gallium RD dump |
| `MESA_SHADER_CACHE_DISABLE=true` | 禁着色器缓存（保证 dump 一致性） |
| `VK_ICD_FILENAMES` | 指定 ICD 清单 |

### 11.5 验证金标准

- **dmesg 0 行**：无 fault / opcode error / protected / hangcheck
- **像素读回**：`center=RGBA(0,255,0,255)` 即纯绿三角形（M1.4 验收标准）
- **nonbg 计数**：非背景像素数（验证三角形确实光栅化）
- **clean exit**：vkWaitForFences OK + vkQueueWaitIdle OK，无 hang

---

## 12. 参考文档索引

| 文档 | 路径 | 用途 |
|---|---|---|
| 项目概览 | [README.md](README.md) | 概览/约束/目录结构/里程碑/部署验证 |
| 开发技术档案 | [GPU_DEVELOPMENT.md](GPU_DEVELOPMENT.md) | 降级命令/验证命令/FAQ/里程碑详情（主参考，36KB） |
| 阶段 0 侦察报告 | [RECON_PHASE0.md](RECON_PHASE0.md) | 设备只读调研（系统概况/驱动栈/Mesa 混装/内核配置/debugfs/sysroot 修复 §8） |
| M1 内核启动 | [M1_KERN_BOOT.md](M1_KERN_BOOT.md) | 内核启动相关 |
| Code Wiki | [CODE_WIKI.md](CODE_WIKI.md) | 本文档（架构/模块/源码/构建/部署/调试全景） |
| H2b 分析笔记 | [work/H2b_gl_vs_tu_varying.md](work/H2b_gl_vs_tu_varying.md) | GL vs tu varying 对比分析 |

### 12.1 关键外部资源

- Mesa 24.0.5 源码：[work/mesa-src/mesa-24.0.5](work/mesa-src)（MIT 许可，本地研究用途）
- a5xx.xml 寄存器定义：[work/mesa-src/mesa-24.0.5/src/freedreno/registers/adreno/a5xx.xml](work/mesa-src)
- GitHub 仓库：https://github.com/lsx1156/gpu

---

**文档维护**：本 Wiki 随项目演进持续更新。每次里程碑达成或重大架构变更时同步修订。最后更新：2026-09-02（M1.5/H2 进行中）。
