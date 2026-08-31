#!/bin/sh
# run9: 清空 dmesg 后连续两次跑 vktriangle，观察是否有新 fault
cd /home/umeko || exit 1
echo 1234 | sudo -S dmesg -C 2>/dev/null
for i in 1 2; do
  echo "===== RUN $i ====="
  timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json ./tu5xx/vktriangle > /tmp/run9_$i.log 2>&1
  echo "exit=$?"
  tail -2 /tmp/run9_$i.log
  sleep 8
done
echo "== new dmesg (after clear) =="
dmesg | grep -E 'fault|opcode|protected|hangcheck' | tail -10
echo "== dmesg line count =="
dmesg | wc -l
echo ===DONE===
