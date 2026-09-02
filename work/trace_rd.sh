#!/bin/sh
D=/sys/kernel/debug/dri/1a01000.display-controller
echo "===GPU_STATE==="
echo 1234 | sudo -S cat $D/gpu 2>&1 | head -80
echo "===RD_STREAM_ATTEMPT==="
echo 1234 | sudo -S sh -c "timeout 15 cat $D/rd > /tmp/krd.rd 2>/tmp/krd.err &"
sleep 1
cd /home/umeko
timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json TU_DEBUG=sysmem MESA_SHADER_CACHE_DISABLE=true ./tu5xx/vktriangle >/dev/null 2>&1
echo "vktriangle exit: $?"
sleep 15
echo "===RD_FILE==="
echo 1234 | sudo -S ls -la /tmp/krd.rd 2>&1
echo 1234 | sudo -S head -c 32 /tmp/krd.rd 2>&1 | od -A x -t x1z
echo "===RD_ERR==="
echo 1234 | sudo -S cat /tmp/krd.err 2>&1
