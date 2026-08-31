#!/bin/sh
# GPU 加速覆盖面审计（只读）
echo '### 1 PROCESSES'
ps -eo pid,pcpu,cmd | grep -E 'Xorg|openbox|picom|xcompmgr|chrom|weston|clutter' | grep -v grep

echo '### 2 XORG LOG'
for lg in /var/log/Xorg.0.log /home/umeko/.local/share/xorg/Xorg.0.log; do
  if [ -f "$lg" ]; then
    echo "--- $lg"
    grep -inE 'driver|glamor|modeset|freedreno|msm|exa|uxa|accel|\(EE\)|\(WW\)' "$lg" | head -60
  fi
done

echo '### 3 GLX'
glxinfo -B 2>&1 | head -30

echo '### 4 EGL'
if which eglinfo >/dev/null 2>&1; then eglinfo 2>&1 | head -60; else echo 'eglinfo not installed'; fi

echo '### 5 MESA/VULKAN PKGS'
dpkg -l 2>/dev/null | grep -iE 'mesa|vulkan|libgl' | awk '{print $1,$2,$3}'

echo '### 6 LIB MAPPING'
ldconfig -p | grep -E 'libGL\.so|libEGL|libgallium|swrast|kms_swrast|libvulkan|freedreno'

echo '### 7 ENV OVERRIDES'
grep -rnE 'LIBGL|GALLIUM|SOFTWARE|MESA' /etc/environment /etc/profile.d/ /home/umeko/.profile /home/umeko/.xinitrc /home/umeko/.xsessionrc /home/umeko/.config/openbox/ 2>/dev/null | head -20

echo '### 8 DRI DEVICES + CLIENTS'
ls -l /dev/dri/ 2>/dev/null
fuser -v /dev/dri/card0 2>&1 | head -10

echo '### 9 VIDEO DECODE'
ls /dev/ 2>/dev/null | grep -E 'v4l|video'
if which vainfo >/dev/null 2>&1; then vainfo 2>&1 | head -8; else echo 'vainfo not installed'; fi

echo '### 10 CHROMIUM LAUNCH'
grep -E '^Exec' /usr/share/applications/chromium*.desktop 2>/dev/null
ls /usr/bin/chromium* /usr/local/bin/chromium* 2>/dev/null
head -10 /usr/local/bin/chromium 2>/dev/null
cat /home/umeko/.config/chromium/chrome-flags.conf 2>/dev/null || echo 'no chrome-flags.conf'

echo '### 11 TOOLS'
which fdperf glmark2-es2 glmark2 es2_info 2>/dev/null

echo '### 12 KERNEL DRM MESSAGES'
dmesg 2>/dev/null | grep -iE 'msm|dpu|mdp|gpu|a5|adreno' | head -25 || echo 'dmesg restricted'

echo '### 13 MEM/THERMAL'
free -m | head -3
for t in /sys/class/thermal/thermal_zone*; do printf '%s: %s\n' "$t" "$(cat $t/type 2>/dev/null)"; done 2>/dev/null | head -10

echo '### AUDIT DONE'
