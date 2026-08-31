# GPU 驱动与支持情况 —— 完整参考文档

> 设备：远程触屏移动终端开发平台（Ubuntu 系）
> 整理日期：2026-08-30
> 用途：后续开发免反复踩坑、免重复探索

---

## 一、设备硬件定位

| 项 | 值 |
|---|---|
| SoC | 高通骁龙 625（MSM8953） |
| GPU | Adreno 506 |
| GPU 能力 | OpenGL ES 3.x、EGL 可用；无 Vulkan 官方支持、无光追 |
| 内核版本 | 6.7.5 |
| 操作系统 | Ubuntu/Debian 移动版（Mobian 系） |
| 图形会话 | Xorg + openbox |

---

## 二、当前驱动栈（已验证的稳定可用状态）

| 层级 | 组件 | 状态 |
|---|---|---|
| 内核侧 | `msm` / `kgsl` DRM 驱动 | 正常，`/dev/dri` 节点正常 |
| OpenGL / EGL（原生 GLES） | **Mesa Freedreno 驱动（fdversion FD506）** | **完全可用**，`glmark2` 实测 **200+ FPS** |
| 2D 加速 | XRender / EXA | 硬件加速支持 |
| Vulkan | Mesa Turnip 驱动 | **不支持 Adreno 506（2026-08-31 已实测：`tu_device.cc:651 device Turnip Adreno 506 unsupported`）** |
| Chromium 渲染 | **GPU 硬件合成**（ANGLE-GLES → Mesa freedreno FD506） | 2026-08-31 修复：加 `--disable-gpu-watchdog` 后 GPU 进程稳定 |

**核心结论：原生 EGL/GLES 链路（Freedreno）是这张卡唯一可靠的硬件加速路径。**

---

## 三、关键版本雷区（最重要，勿动）

### 3.1 Mesa 版本兼容性问题（2026-08-30 阶段 0 侦察修正）

- **修正**：`DRM_MSM_GEM_INFO -22` 在 **24.0.5 下同样存在**，但只是**非致命警告**（BO metadata 命名失败被容忍，GL 照常工作）；26.1.7 下同样的调用成为**致命错误**导致 GPU 进程崩溃。
- **混装现状**：加载器层已降至 24.0.5 并实际生效，但 `mesa-libgallium`/`mesa-common-dev`/`mesa-vdpau-drivers` 仍残留 26.1.7~kisak1（当时降级命令漏了这几个包），属待清理项。
- 运行时实测（glxinfo，2026-08-30）：`FD506 / Mesa 24.0.5-1ubuntu1 / GL 3.1 / GLES 3.1 / direct rendering: Yes / Accelerated: yes`。
- 内核为自编译定制版：`6.7.5-rv2-umeko-msm8953+`，容器内交叉编译产物，源码构建链有先例。详见 `RECON_PHASE0.md`。

### 3.2 Mesa 降级命令（已执行，可复用）

```bash
sudo apt-get install -y --allow-downgrades \
  libgl1-mesa-dri=24.0.5-1ubuntu1 \
  libegl-mesa0=24.0.5-1ubuntu1 \
  libgbm1=24.0.5-1ubuntu1 \
  libglx-mesa0=24.0.5-1ubuntu1 \
  libglapi-mesa=24.0.5-1ubuntu1 \
  mesa-vulkan-drivers=24.0.5-1ubuntu1
```

### 3.3 禁忌

- **不要升级 Mesa** —— 动 Mesa 版本百分百复发 `DRM_MSM_GEM_INFO -22`。
- **不要添加 kisak PPA** —— 会把系统拉回 26.x 的坑。

---

## 四、Chromium 渲染配置终版（2026-08-31 ANGLE 专项结案定版）

**终版结论（四层）：**
1. **GPU watchdog 误杀**（上午修复，仍有效）：`--disable-gpu-watchdog` 必需，否则 GPU 进程初始化 >30s 被 watchdog 爆破（exit_code=512）。
2. **硬件合成黑屏**（下午实证）：去掉 `--disable-gpu-compositing` 后 GPU 进程稳定、无新 dump，但**物理屏全黑**——Chromium 内容走 DRM overlay plane 后**提交不到物理面板**。**`--disable-gpu-compositing` 必须保留（软件合成）**。
3. **ANGLE 硬件加速判死**（傍晚专项结案，见下节）：ANGLE 全后端在此设备不可用，Chromium 151 Linux 仅 ANGLE 一个 GL 后端 → **Chromium 硬件加速在当前驱动栈上无解**。渲染定版 SwiftShader 软渲染。
4. **缩放**：`--force-device-scale-factor=1.5` 已移除（软渲染下渲染量 ×2.25，代价过高，降回 1x）。

### ANGLE 专项结案（2026-08-31 傍晚，drmm-engine=0 铁证）

**失败矩阵（t5-t9 五组实验）：**

| 配置 | ANGLE 行为 | 结果 |
|---|---|---|
| gles + GPU沙箱 | 快速报错（12289） | 软渲染（快速失败）✓ 可用 |
| gles + --no-sandbox / --disable-gpu-sandbox | **0 报错 ≠ 成功**：卡进 msm_dri 超慢循环 **~22 分钟**（gdb 实证主线程 R 状态自旋于 msm_dri.so 文件偏移 0xA44AC4 一带，LR 0xA45130-58，双层 (n,m) 枚举循环） | 最终"完成"但 **drm-engine-gpu: 0 ns / drm-cycles-gpu: 0**（fdinfo 铁证）——**从未提交任何 GPU 命令**，纯浪费 22 分钟 |
| gl（桌面GL/GLX）+ 任意沙箱 | 快速报错（16 次） | use-gl=disabled，软渲染 |
| **swiftshader（终版）** | 不触碰系统 GL | GPU 进程正常（state=S），kiosk 热缓存 **34 秒加载完成** ✓ |

**关键教训：**
- "ANGLE 报错 0 次"不等于初始化成功——t5/t6 实验只 grep 了错误行数，进程其实卡在 22 分钟循环里还没走到报错点
- 验证硬件加速的金标准是 **/proc/<gpu_pid>/fdinfo 的 drm-engine-gpu**，不是日志
- msm_dri.so 的 22 分钟死循环（stripped，函数未命名）只在 EGL platform 路径触发；标准 X11 EGL 序列（eglprobe 自制探针：Initialize→ChooseConfig→ES3 Context→MakeCurrent）与 surfaceless 路径**均正常秒过**，glmark2 亦正常——**Chromium GPU 进程的 ANGLE 调用序列触发了 freedreno 某条特殊路径**，待 tu5xx/Mesa 升级后再回头查
- 探针工具留存：`work/eglprobe.c`（X11 标准序列）、`work/eglprobe2.c`（surfaceless/GBM/device）、`work/msm_dri.so`（PC 留档，死循环偏移 0xA44AC4）

**内核侧旁证（与本问题无关但需记档）：** 内核 6.7.5-rv2 存在 `block_devnode+0x4` NULL deref Oops（fwupd 读块设备 uevent 触发，开机 1 小时内发生 5 次，Tainted: D W）——上游主线已知问题类别，M1 内核阶段可顺手补修。另 `fb_set_par error -16 (EBUSY)` 反复出现，与 Xorg fbdev 交互相关，暂无实际影响。

### 证据链（watchdog 修复，上午）

- 35 份 crashpad 转储 PC 完全一致（chromium+0xff65d48，libc abort 调用链）→ 确定性主动爆破，非野指针
- 崩溃周期精确 30s；dmesg 全程无 adreno fault/hang → 不是 GPU 挂死，是 watchdog 判死
- `--no-sandbox` 对照实验：照崩 → 排除沙箱
- TU (Turnip) 报错仅为 GPU info 收集探测失败（VK_ERROR_INITIALIZATION_FAILED，良性 handled）
- `MESA: warning: DRM_MSM_GEM_INFO -22` 在 24.0.5 下确认为非致命警告，与崩溃无关

### 证据链（硬件合成黑屏，下午）

- 实验步骤：sed 去掉 `--disable-gpu-compositing`（备份 .bak-hwcomp-20260831）→ pkill 重启 → GPU 进程稳定、pending 目录无新 dump（最新 dump 为 12:45 旧文件）
- scrot 截图 8.5KB（纯 openbox 桌面），用户目视确认**物理屏全黑**、触摸有鼠标 → overlay 内容到不了面板
- 回退后（恢复 --disable-gpu-compositing）物理屏立即恢复显示
- 结论：**此组合不可用，勿再尝试**；除非未来修复 Xorg msm overlay 提交（内核/驱动层）

### 证据链（ANGLE EGL 失败，下午）

- `--enable-logging=stderr` 抓取：`Could not create a backing OpenGL context`（angle_platform_impl.cc）+ `eglInitialize: EGL_NOT_INITIALIZED ... trying next display type`
- **ldd 实证**：libEGL_mesa.so.0 / libgbm.so.1 / msm_dri.so 均 **NEEDED 无 libgallium**（静态链接），与 mesa-libgallium 删除无关（该包已删，libgallium 文件归零，glxinfo/eglinfo 全绿：FD506 / GL 3.1 / EGL 1.5 / direct rendering: Yes）
- Grafana 前端 JS 存活且查询 11s 即成功返回（容器日志 queryData status=ok），但面板 canvas 需 2-4 分钟才逐步画完（软栅格化速度）——**"卡 Loading/空图表"是初始化慢，不是故障**，判断前等 ≥3 分钟再截图

### 排除项（勿再怀疑）

1. **沙箱**：`--no-sandbox` 实证无效（已回退，沙箱保持开启）
2. **Mesa 26.1.7 混装**：已于 2026-08-31 13:27 清理完毕（移除 mesa-libgallium / mesa-common-dev / mesa-vdpau-drivers 及 va-driver-all / vdpau-driver-all 空元包），GL 运行时验证完好。三个库 ldd 均无 libgallium 依赖
3. **Grafana 数据链路**：Chromium→Grafana API→Prometheus(:9090 绑 *)→netdata(127.0.0.1:19999，宿主机进程) 全链路 curl 实证通畅；targets 全 UP
4. **profile 缓存**：全新 user-data-dir 实例同样现象，排除
5. **内存/zram**：available 1.1GB、si/so≈0、无 OOM，排除

### 联网请求处理（2026-08-31 定版）

- `/etc/chromium.d/extensions` 已移除 `--enable-remote-extensions`（⚠️ 改此目录时**勿把 .bak 留在同目录**——wrapper `for file in /etc/chromium.d/*` 会通配 source，备份文件里的 flag 会复活；备份已移至 ~/extensions.bak-20260831）
- `.xinitrc` 已加 `--disable-background-networking`
- 残余 SYN-SENT（crashpad 上传 pending dump 等）为异步行为，**不阻塞页面加载**（实证：有 SYN-SENT 时页面照常渲染完成）

### 已应用的最终配置（.xinitrc，快照存 device_conf/）

```bash
exec /usr/bin/chromium --kiosk --noerrdialogs --lang=zh-CN --disable-translate \
  --disable-gpu-watchdog --disable-gpu-compositing --disable-background-networking \
  --use-angle=swiftshader \
  --user-data-dir=/home/umeko/.kiosk \
  http://127.0.0.1:3000/d/netdata-system-zh?kiosk
```

### 已知代价与运维注意

- watchdog 禁用后，GPU 进程**真挂死不会自愈**（画面冻结但进程在）→ 处置：`pkill -f '/usr/lib/chromium/chromium'`，getty autologin 循环会自动拉起整个 X + kiosk（这就是设备的"看门狗"）
- **Chromium 重启后面板冷缓存需 2-4 分钟渲染完成**（SwiftShader 软栅格化），热缓存 ~35 秒；属正常现象，勿误判为故障反复重启
- **勿再加 `--enable-gpu --use-angle=gles`（尤其配 --disable-gpu-sandbox）**：会触发 msm_dri 22 分钟假成功循环，页面 23 分钟才出来且 GPU 用量为 0
- ANGLE 硬件加速已判死（见上文失败矩阵）；除非未来 ① Mesa 升级修复 freedreno EGL 循环 ② tu5xx 落地 + Chromium Vulkan 路径，否则软渲染即终态

---

## 四·补 freedreno 用户态审计与落地（2026-08-31）

**来源**：对照一份"用户态开启 freedreno（不改内核）"通用指南逐项核对本机。**结论：九成已满足，唯一落地项为 CPU 调度器。**

### 逐项核对（只读审计，`work/perf_audit.sh`）

| 指南项 | 本机现状 | 结论 |
|---|---|---|
| render node 权限 | `renderD128` 属组 `render`，用户已在 `render`+`video` 组 | ✅ 已满足 |
| 固件 | `/lib/firmware/a506_zap.*` 已就位（A506 无 GMU，仅 zap） | ✅ 已满足 |
| 闭源 blob | DRI 目录仅开源 `msm/swrast/kms_swrast`，无 qcom 闭源 GLES/EGL | ✅ 无需处理 |
| 环境变量 | 无 GALLIUM/SOFTWARE/MESA 覆盖残留 | ✅ 干净 |
| GPU 调度 | devfreq `simple_ondemand`（无 performance 可选），空闲 200MHz 属正常提频机制 | ✅ 已优 |
| 热管理 | CPU 40–43°C / GPU 39°C | ✅ 无 throttling |
| Mesa 版本 | 全套 24.0.5，无 26.1.7 残留 | ✅ 已锁 |
| **CPU 调度器** | 4 核全 `performance`（可选 schedutil/ondemand/userspace） | ⚠️ **唯一落地项** |

### 落地（2026-08-31）

- **CPU governor `performance` → `schedutil`**（最佳性能/功耗平衡；`performance` 恒满频，对持续 kiosk 的电池终端耗电升温）。
- 运行态已应用 4 核全 schedutil；持久化走 systemd 服务 `cpu-governor.service`（oneshot，`WantedBy=multi-user.target`，已 enable+active，重启自动生效）。
- 提醒：改系统库/写 /etc 下的持久化文件，**别在本会话用 plink 传 heredoc**（CRLF 会破坏正文，曾制造 0 字节 masked 服务）；统一改为**本地写文件 → pscp 推送 → 远端直跑小脚本**。

### Vulkan 实证探针（vulkan 尝试，2026-08-31）

用 dlopen 版最小枚举器 `work/vkenum.c`（交叉编译，无 Vulkan 头依赖）实测：

| ICD | 结果 |
|---|---|
| `freedreno_icd.aarch64.json`（**Turnip** HW Vulkan） | `vkCreateInstance` SUCCESS，但 **`tu_device.cc:651: device Turnip Adreno (TM) 506 is unsupported (VK_ERROR_INITIALIZATION_FAILED)`** → 物理设备枚举为 **0**，**硬件级不支持** |
| `lvp_icd.aarch64.json`（llvmpipe 软件 Vulkan） | **Segmentation fault / core dump**（此设备上软件 Vulkan 也崩） |
| 默认全部 ICD | turnip 报错 + 枚举 0 设备 |

**定论：本机 Vulkan 两层均不可用** —— 无硬件 Vulkan（Turnip 拒绝 a5xx/A506），软件 Vulkan（lvp）亦崩溃。GLES/EGL 仍是唯一可用加速路径。

---

## 四·补2 M1：给 freedreno 写 a5xx（tu5xx）Vulkan 后端（M1.0 起步，2026-08-31）

### 立项背景
Turnip 从 tu6xx 起才支持 Adreno 6xx+，a5xx（含 A506）无 Vulkan 后端。M1 的目标是为 freedreno 自研 a5xx Vulkan 后端（tu5xx），分里程碑推进，M1.0 = 先打通 **Mesa 24.0.5 交叉编译** 可构建基线。

### M1.0 产出一：Mesa 24.0.5 交叉编译打通（2026-08-31）
- 平台：PC（x86_64, Windows）→ aarch64；工具链 `tools/bin/aarch64-none-linux-gnu-*`（ARM 官方 15.2.Rel1）+ 设备 sysroot（`sysroot/`）。
- 复用并封装为 `mesa-cross/cross-aarch64.ini`（cross-file）与 `mesa-cross/build-mesa.ps1`（一键 setup/reconf/build）。
- 构建开关：`-Dgallium-drivers=freedreno -Dvulkan-drivers=freedreno`，关 opengl/gles/llvm/video/X11（`platforms=` 空、egl/glx/gbm disabled），仅产出 freedreno GL + Turnip Vulkan。
- **核心成功产物**：`work/mesa-build/src/freedreno/vulkan/libvulkan_freedreno.so`（**8.8MB**，readelf 证实 ELF64 little-endian，`Type: DYN / Machine: AArch64`），即 turnip 的 Vulkan 后端用户态库；配套 ICD `freedreno_icd.aarch64.json` 亦由 meson 生成。
- M1.0 里程碑验收 = **Mesa 24.0.5 可交叉构建出 turnip `.so`**，已达标。

### M1.0 遇到并已解决的坑（供复用）
- **C++ 编译参数未生效**：`cross-aarch64.ini` 中 `cpp_args/c_link_args/cpp_link_args` 若缩进到`[properties]`内会被 meson 忽略，必须顶格（列 0）写在 `[properties]` 下，且系统多架构头需 `-isystem sysroot/usr/include/aarch64-linux-gnu` 显式指路。
- **pkgconf 3.0.5 拼错 multiarch libdir**：`PKG_CONFIG_SYSROOT_DIR` 开启时，libz/libdrm/libexpat 的 `-L` 被拼成 `sysroot/usr/lib/**lib**/aarch64-linux-gnu`（多一层 `lib/`），导致 `-lz/-ldrm/-lexpat` 找不到。非持久解法：在 `sysroot/usr/lib/lib/aarch64-linux-gnu` 建目录放入实体库副本（**不能用 junction**——junction 内 symlink 无法被 GNU ld 解析，报 `file not recognized`）。
- **libexpat.so.1.9.1 损坏**：sysroot 内该文件被提取时损坏（readelf `Machine: <unknown>: 0xbfef` + `e_shentsize` 小于 section header 大小）。从设备重拉干净副本（198712B）替换后链接通过。
- 失败工具（`fd5_layout/fd6_layout/ir3_delay_test/ir3_disasm` 等）仅是 `-Dbuild-tests=false` 未覆盖的旧测试目标，非驱动核心，不影响 `.so` 产出。

### M1.1 待办（未做）
- 改 turnip 门禁（`tu_device.cc`）允许 a5xx/A506 枚举，让 vkenum 能列出物理设备，再逐层搭建 tu5xx 命令流后端。

---

## 五、开发路线建议（避免反复造轮子）

| 需求 | 推荐路线 | 说明 |
|---|---|---|
| 3D / GLES 原生加速 | **原生 EGL/GLES**（SDL2 + GLES2、Qt、或 WebKitGTK 硬件加速路径） | 这条链路已验证可用（glmark2 200+ FPS） |
| 跑 Chromium / Web 界面 | 软件渲染 + GPU 光栅化尝试（现状）→ **待专项修复 ANGLE EGL** | 硬件合成黑屏已证伪勿试；ANGLE EGL 失败是当前最大优化点 |
| Vulkan | **放弃** | Adreno 506 无 Turnip 支持 |
| 2D 界面 | XRender / EXA 硬件加速可用 | Xorg + openbox 环境正常 |

### 综合开发检查清单

- [x] 驱动已固定在 Mesa 24.0.5，勿升级、勿加 kisak PPA（26.1.7 残留已清理完毕 2026-08-31，ldd 证实三库无 libgallium 依赖）
- [x] Chromium 稳定配置已定版（2026-08-31）：`--disable-gpu-watchdog --disable-gpu-compositing --disable-background-networking`（watchdog 防误杀；硬件合成黑屏实证勿再试；联网组件已断）
- [ ] **待专项：ANGLE EGL 初始化失败**（`Could not create a backing OpenGL context`）——Chromium 硬件加速实际未生效，当前为软栅格化；修复后 Grafana/Web 渲染可望大幅提速
- [ ] 硬件合成黑屏（DRM overlay 提交不到面板）——需要内核/Xorg 驱动层修复，长期项
- [ ] zram 已生效：`/etc/default/zramswap`（lz4, PERCENT=50 ≈ 1.4G），重启实测 swapon 正常
- [ ] 会话/配置文件中禁止出现 `LIBGL_ALWAYS_SOFTWARE` / `GALLIUM_DRIVER=llvmpipe`（已清理，勿再加）
- [ ] Web 缩放用 `zoom`，不用 `transform: scale()`（软渲染下合成层会黑屏；GPU 合成开启后此限制待复测）
- [ ] 需要 GPU 加速的场景优先考虑原生 EGL/GLES，而非浏览器
- [ ] Vulkan 相关需求直接排除

---

## 六、历史探索结论速查（FAQ）

**Q1：GPU 能不能用？**
能。原生 EGL/GLES 走 Freedreno 完全可用，glmark2 200+ FPS。

**Q2：为什么 Chromium 黑屏/崩溃？**
（2026-08-31 终版）真凶是 **Chromium GPU watchdog**：GPU 进程初始化（Mesa/ANGLE 在 A53 上 >30s）被判挂死遭爆破（exit_code=512，固定 PC 静默 abort）。加 `--disable-gpu-watchdog` 修复，现 GPU 硬件合成稳定运行。早期"废弃参数 `--use-gl=egl`"是其中一层诱因，已一并移除。

**Q3：为什么不用 Mesa 26.x？**
与内核 6.7.5 不兼容，`DRM_MSM_GEM_INFO` 返回 -22。必须锁 24.0.5。

**Q4：Turnip 能用吗？**
不能。Turnip 仅支持 Adreno 6xx 系列及以上。

**Q5：2D 加速可用吗？**
可用。XRender / EXA 硬件加速支持，Xorg + openbox 正常。

---

## 七、验证命令速查

```bash
# 查看 GPU 驱动信息
cat /sys/class/drm/card0/device/gpuinfo

# 查看 Mesa/EGL 版本
eglinfo | grep -i "fdversion\|GL_VERSION"

# 确认 Mesa 版本未被升级（应显示 24.0.5）
dpkg -l | grep -E "libgl1-mesa-dri|libegl-mesa0" 

# DRM 节点检查
ls -la /dev/dri/

# 原生 GLES 基准测试
glmark2
```
