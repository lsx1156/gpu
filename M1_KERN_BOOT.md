# M1：内核源码与 boot.img 引导链摸底

> 日期：2026-08-30。方式：PC 侧分析备份分区镜像（只读）+ 设备侧只读查询 + 本地发行包分析。

---

## 1. 引导链（最终版，已逐项验证）

```
PBL → sbl1(p4) → aboot/LK(p19) → boot.img(p21) = 定制版 lk2nd
    → lk2nd 从 rootfs ext4 /boot/ 加载 vmlinuz-6.7.5 + initrd + dtb
    → mainline 内核 → Ubuntu 24.04
```

关键证据：
1. p21 boot.img（v0 头）kernel 区 = gzip("lk.bin")，224KB，cmdline 字段="lk2nd"；
2. **设备版 lk2nd ≠ 发行包通用版**（MD5 不同），其镜像内嵌完整 cmdline：
   `console=tty0 root=UUID=93afcbbe-875f-49b0-83da-ee8193f20ca5 rw loglevel=3 splash`
   与设备 /proc/cmdline 逐字一致 —— **cmdline 修改点在 lk2nd，不在内核**；
3. p22（recovery 分区）= **原厂 MIUI 3.18.31-perf 内核残留**（builder@c3-miui-ota-bd21.bj 构建，gzip 24MB 已解压验证），与当前启动无关；
4. CONFIG_CMDLINE=""（内核无强制 cmdline），排除内核侧配置。

**结论（对内核实验的重大利好）**：
- **换内核 = 替换 rootfs /boot/vmlinuz-*（+dtb/initrd），不动任何分区**，保留旧文件即可秒回滚；
- 改 cmdline（如开串口 console=ttyMSM0）需要处理 lk2nd —— 途径：a) 重打定制 lk2nd（需 lk2nd 源码）；b) 验证 lk2nd 是否支持 extlinux.conf 等文件级配置（COM35 串口上 lk2nd 有交互菜单，待实测）；
- "625完美桌面"发行包位于 `D:\625完美桌面\625完美桌面\`（lk2nd.img 通用版 + rootfs.img + 625_boot/ 各触屏变体 + flash.bat 流程：先刷 lk2nd 再在其 fastboot 里刷 boot 变体）；mido 有 ft5406/gt917d 两个触屏芯片变体。

## 2. 分区布局（A-only 单槽，修正阶段 0 说法）

- **boot = p21（唯一 boot，64M）**；p22 = **recovery**（非 boot_b，现为原厂残留）
- aboot = p19 + abootbak = p20（LK 主/备）；devinfo = p23；misc = p27
- 设备 mkbootimg/unpack 工具未装 → 打包/解包统一在 PC 侧做

## 3. 内核侧现状

- 版本：`6.7.5-rv2-umeko-msm8953+ #7 SMP PREEMPT`（2024-02-23）
- 构建：容器内交叉编译 `root@97df89c06e5c`，aarch64-linux-gnu-gcc 9.4.0（Ubuntu 20.04 工具链，binutils 2.34）
- /boot/ 全套齐全（config/System.map/vmlinuz/initrd），initrd.img mtime 2024-08-30（今晨被重建过，原因待查）
- /proc/cmdline 无 console=ttyMSM0 → 内核日志默认不上 COM35 串口

## 4. 内核源码定位结论

- 设备 /usr/src/ 的树 = kernel-package 裁剪 headers（106MB，msm 驱动无 .c）→ 不可重编；
- 可用情报：.config 完整（192KB）、CONFIG_LOCALVERSION="-rv2-umeko-msm8953"（AUTO 未设，手工版本）、revision "10.00.Custom"（kernel-package 流程）；
- 判定：原内核 ≈ **vanilla 6.7.5 + 设备 .config**（GPU 行为实测与 vanilla 一致），无隐藏源码补丁；
- **内核基线 = kernel.org v6.7.5 vanilla + 设备 .config**，git 管理后叠加我们的 drm/msm 补丁。

## 5. PC 侧资产

- `work/img/midobackup/{boot,recovery}.img`（64M 全镜像，SHA256 校验过的备份）
- `D:\625完美桌面\625完美桌面\`（发行包：lk2nd 通用版、rootfs.img、触屏变体 boot）
- 下一步：PC 侧 mkbootimg/unpackbootimg 工具链；v6.7.5 源码拉取；WSL2 修复（DISM /RestoreHealth）
