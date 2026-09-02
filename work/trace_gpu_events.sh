#!/bin/sh
echo 1234 | sudo -S dmesg -c >/dev/null 2>&1
cd /home/umeko
timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json TU_DEBUG=sysmem MESA_SHADER_CACHE_DISABLE=true ./tu5xx/vktriangle >/dev/null 2>&1
echo "vktriangle exit: $?"
echo ===GPU_EVENTS_ONLY===
echo 1234 | sudo -S dmesg 2>&1 | grep -viE "drm_ioctl|drm_open_helper|drm_release|drm_file_free" | grep -iE "msm|gpu|fence|submit|irq|hang|freq|perf|pm_|error"
echo ===PERF_COUNTERS===
echo 1234 | sudo -S cat /sys/kernel/debug/dri/1a01000.display-controller/perf 2>&1 | head -50
