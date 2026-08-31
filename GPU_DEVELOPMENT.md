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
| Chromium 渲染 | CPU 软渲染 | 当前稳定方案（稳定优先，禁用 GPU 合成） |

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

## 四、Chromium 为何只能软渲染（2026-08-31 根因修正）

**真正的死因（~/.kiosk-x.log 铁证）**：

```
ERROR:ui/gl/init/gl_factory.cc:110] Requested GL implementation (gl=none,angle=none)
  not found in allowed implementations: [(gl=egl-angle,angle=default)].
ERROR:components/viz/.../viz_main_impl.cc:190] Exiting GPU process due to errors during initialization
```

1. **`--use-gl=egl` 是已废除的老参数**：现版本 Chromium 将其解析为 `gl=none,angle=none`，不在允许列表中 → GPU 进程反复启动即崩（即历史上的 exit_code=512），随后 chromium 内部以 `--use-gl=disabled` 降级运行。与 Turnip/内核 -22 无关。
2. **旧结论修正**：`.profile`/`.xsessionrc` 里堆叠的 `LIBGL_ALWAYS_SOFTWARE=1` / `GALLIUM_DRIVER=llvmpipe` / `QT_XCB_FORCE_SOFTWARE_OPENGL=1` 属"休眠地雷"——kiosk 启动链（login → .bash_profile → startx → .xinitrc）并不读取它们，活会话进程环境实测干净。已全部清理。
3. GPU 进程存活后若仍有 -22 相关崩溃，再回头清理 Mesa 26.1.7 gallium 混装（候选手段）。

**已应用的修复（2026-08-31，待重启验证）**（设备侧均有 .bak-20260831 备份，快照存 device_conf/）：

- `.xinitrc`：删除 `--use-gl=egl --use-angle=native --enable-gpu-compositing=false --disable-gpu-rasterization`，仅保留 `--enable-gpu`（走默认 egl-angle → Mesa EGL → freedreno FD506）
- `.profile` / `.xsessionrc`：删除全部软渲染变量（保留 QT 缩放、CLUTTER_BACKEND=glx）
- 新增 zram：`/etc/default/zramswap`（ALGO=lz4, PERCENT=50 ≈ 1.4G），zramswap.service 开机自启

**若重启后 GPU 进程仍崩溃的回退方案**（旧经验参数）：

```bash
--disable-gpu --disable-gpu-compositing --disable-gpu-rasterization
```

---

## 五、开发路线建议（避免反复造轮子）

| 需求 | 推荐路线 | 说明 |
|---|---|---|
| 3D / GLES 原生加速 | **原生 EGL/GLES**（SDL2 + GLES2、Qt、或 WebKitGTK 硬件加速路径） | 这条链路已验证可用（glmark2 200+ FPS） |
| 跑 Chromium / Web 界面 | **认了软渲染**，带禁用 GPU 参数 | 用 `zoom` 缩放，**别用 `transform: scale()`**（软渲染下合成层会黑屏） |
| Vulkan | **放弃** | Adreno 506 无 Turnip 支持 |
| 2D 界面 | XRender / EXA 硬件加速可用 | Xorg + openbox 环境正常 |

### 综合开发检查清单

- [ ] 驱动已固定在 Mesa 24.0.5，勿升级、勿加 kisak PPA（残留 26.1.7 gallium 待清理）
- [ ] Chromium 启动参数已去掉 `--use-gl=egl`（老参数，GPU 进程死因），当前 `--enable-gpu`；重启验证 GPU 进程存活，失败则回退 `--disable-gpu --disable-gpu-compositing --disable-gpu-rasterization`
- [ ] 会话/配置文件中禁止出现 `LIBGL_ALWAYS_SOFTWARE` / `GALLIUM_DRIVER=llvmpipe`（已清理，勿再加）
- [ ] Web 缩放用 `zoom`，不用 `transform: scale()`（软渲染下合成层会黑屏；GPU 合成开启后此限制待复测）
- [ ] 需要 GPU 加速的场景优先考虑原生 EGL/GLES，而非浏览器
- [ ] Vulkan 相关需求直接排除

---

## 六、历史探索结论速查（FAQ）

**Q1：GPU 能不能用？**
能。原生 EGL/GLES 走 Freedreno 完全可用，glmark2 200+ FPS。

**Q2：为什么 Chromium 黑屏/崩溃？**
（2026-08-31 修正）真凶是启动参数 `--use-gl=egl`——已废除的老参数，被解析为 `gl=none,angle=none`，GPU 进程启动即崩（exit_code=512）。已修复待验证。

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
