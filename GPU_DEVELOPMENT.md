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
| Vulkan | Mesa Turnip 驱动 | **不支持 Adreno 506，别抱希望** |
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

## 四、Chromium GPU 加速修复实录（2026-08-31 终版根因：GPU watchdog 误杀）

**终版结论：GPU 进程初始化（ANGLE 重试 + Mesa freedreno 在 A53 上 >30s）超过 Chromium GPU watchdog 阈值，watchdog 误判挂死并爆破进程（固定 PC 静默 abort，exit_code=512）。加 `--disable-gpu-watchdog` 后 GPU 进程稳定，硬件合成生效。**

### 证据链（32+ 份 minidump + xlog + 对照实验）

- 35 份 crashpad 转储 PC 完全一致（chromium+0xff65d48，libc abort 调用链）→ 确定性主动爆破，非野指针
- 崩溃周期精确 30s；dmesg 全程无 adreno fault/hang → 不是 GPU 挂死，是 watchdog 判死
- `--no-sandbox` 对照实验：照崩 → 排除沙箱
- verbose 日志 SwiftShader 出现 0 次 + MESA BO 警告（freedreno 在跑）→ 硬件路径确认
- TU (Turnip) 报错仅为 GPU info 收集探测失败（VK_ERROR_INITIALIZATION_FAILED，良性 handled）
- `MESA: warning: DRM_MSM_GEM_INFO -22` 在 24.0.5 下确认为非致命警告，与崩溃无关

### 排除项（勿再怀疑）

1. **沙箱**：`--no-sandbox` 实证无效（已回退，沙箱保持开启）
2. **Mesa 26.1.7 混装**：`msm_dri.so`（24.0.5 noble 构建）为静态链接，NEEDED 无 libgallium → 26.1.7 残留（mesa-libgallium/mesa-common-dev/mesa-vdpau-drivers）**不在 GL 运行时路径**，仅待清理防患
3. **SwiftShader/软渲染地雷**：环境变量已清，verbose 日志 0 次出现
4. **Turnip**：探测失败即被 Chromium 妥善处理，非死因

### 已应用的最终修复（.xinitrc，设备侧 .bak 备份齐全，快照存 device_conf/）

```bash
exec /usr/bin/chromium --kiosk --noerrdialogs --lang=zh-CN --disable-translate \
  --disable-gpu-watchdog --force-device-scale-factor=1.5 \
  --user-data-dir=/home/umeko/.kiosk --enable-gpu --use-angle=gles \
  http://127.0.0.1:3000/d/netdata-system-zh?kiosk
```

- 新增 `--disable-gpu-watchdog`（核心修复）
- 删除 `--disable-frame-rate-limit`（软渲染时代参数；HW 合成下不限帧会烧 200% CPU，去掉后 vsync 限 60fps，load 3.05→1.07）

### 已知代价与运维注意

- watchdog 禁用后，GPU 进程**真挂死不会自愈**（画面冻结但进程在）→ 处置：`pkill -f '/usr/lib/chromium/chromium'`，getty autologin 循环会自动拉起整个 X + kiosk（这就是设备的"看门狗"）
- 若未来换更快启动路径（如 Mesa 初始化提速），可尝试撤掉此参数复测

**历史回退方案（已不需要）**：`--disable-gpu --disable-gpu-compositing --disable-gpu-rasterization`

---

## 五、开发路线建议（避免反复造轮子）

| 需求 | 推荐路线 | 说明 |
|---|---|---|
| 3D / GLES 原生加速 | **原生 EGL/GLES**（SDL2 + GLES2、Qt、或 WebKitGTK 硬件加速路径） | 这条链路已验证可用（glmark2 200+ FPS） |
| 跑 Chromium / Web 界面 | **GPU 硬件合成**（`--enable-gpu --use-angle=gles --disable-gpu-watchdog`） | 2026-08-31 起可用；`transform: scale()` 黑屏风险待复测，缩放仍建议 `zoom` |
| Vulkan | **放弃** | Adreno 506 无 Turnip 支持 |
| 2D 界面 | XRender / EXA 硬件加速可用 | Xorg + openbox 环境正常 |

### 综合开发检查清单

- [ ] 驱动已固定在 Mesa 24.0.5，勿升级、勿加 kisak PPA（残留 26.1.7 gallium 已证实不在 GL 运行时路径，待清理防患）
- [x] Chromium GPU 加速已修复（2026-08-31）：`.xinitrc` 参数 `--enable-gpu --use-angle=gles --disable-gpu-watchdog`；GPU 进程稳定，无 use-gl=disabled，SwiftShader 0 次；已删 `--disable-frame-rate-limit`（HW 下不限帧烧 200% CPU）；GPU 进程若真挂死，`pkill -f '/usr/lib/chromium/chromium'` 后 getty autologin 会自动拉起
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
