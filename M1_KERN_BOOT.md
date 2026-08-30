# M1：内核源码与 boot.img 引导链摸底

> 日期：2026-08-30。方式：PC 侧分析备份分区镜像（只读）+ 设备侧只读查询。

---

## 1. 引导链（已完整还原）

```
PBL → sbl1(p4) → aboot/LK(p19, 备份 p20) → boot.img(p21)
    → boot.img "kernel" = gzip("lk.bin") = lk2nd（第二级 bootloader）
    → lk2nd 从 ext4 rootfs /boot/ 加载 {vmlinuz, initrd.img, dtb}
    → mainline 6.7.5 内核 → Ubuntu 24.04
```

**证据链**（boot.img 头解析，v0 标准 2K pagesize）：

| 字段 | 值 | 说明 |
|---|---|---|
| kernel_size | 0x36CC3 = 224,451 | gzip 流，FNAME 内嵌 **"lk.bin"** —— 不是 Linux 内核，是 lk2nd |
| kernel_addr | 0x80008000 | mido 标准 |
| ramdisk_size | 0 | 无 ramdisk |
| tags_addr | 0x80000100 | mido 标准 |
| dt_size | 0x15000 = 86,016 | DT 区 magic = **QCDT**（高通老式多 DT blob） |
| cmdline 字段 | `lk2nd` | 实锤：boot.img 装的就是 lk2nd |

## 2. 分区布局（A-only 单槽，修正阶段 0 说法）

- **boot = p21（唯一 boot，64M）**；p22 = **recovery**（非 boot_b）
- aboot = p19 + abootbak = p20（LK 主/备）
- devinfo = p23；misc = p27；完整 partlabel 表见备份 midobackup.tar.gz
- 设备 mkbootimg/unpack 工具未装 → 打包/解包统一在 PC 侧做

## 3. 内核侧现状

- 版本：`6.7.5-rv2-umeko-msm8953+ #7 SMP PREEMPT`（2024-02-23）
- 构建：容器内交叉编译 `root@97df89c06e5c`，aarch64-linux-gnu-gcc 9.4.0（Ubuntu 20.04 工具链，binutils 2.34）
- /boot/ 全套齐全（config/System.map/vmlinuz/initrd），initrd.img mtime 2024-08-30（今晨被重建过，原因待查）
- **/proc/cmdline：`console=tty0 root=UUID=93afcbbe-... rw loglevel=3 splash`**
  → 内核 console 未指向串口（COM35 上内核日志默认不可见，lk2nd/aboot 阶段日志待实测）
  → 开串口内核 console 是后续内核重编/引导参数实验项

## 4. 对 tu5xx / 内核工作的影响

1. **内核重编路径已明**：PC 侧 mkbootimg 打包的对象是 lk2nd（boot.img），自编内核放 rootfs /boot/ 由 lk2nd 加载 —— **改内核不需要动 boot.img**（除非换 dtb 交付方式），实验风险大幅降低；
2. dtb 交付：lk2nd 自带/从 qcdt 选择 dtb，内核树里的 msm8953 dtb 与之的匹配关系待核；
3. 串口调试：内核日志要上 COM35 需 cmdline 加 `console=ttyMSM0,115200n8`（lk2nd 是否透传内核 cmdline 待实测）；
4. 待用户确认：内核源码仓库（"rv2" 含义）、lk2nd 构建产物来源。

## 5. PC 侧已有资产

- `work/img/midobackup/boot.img`、`recovery.img`（64M 全镜像，来自 SHA256 校验过的备份）
- 下一步：PC 侧搭 mkbootimg/unpackbootimg（pip install mkbootimg 或源码编译），复现"解包-重打包-字节级一致"验证
