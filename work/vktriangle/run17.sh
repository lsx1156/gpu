#!/bin/sh
# run17: 多彩版(varying)抓 RD dump，供与 M1.4 常量绿(run15/16)差分 VPC/SP 寄存器
cd /home/umeko || exit 1
echo 1234 | sudo -S dmesg -C 2>/dev/null
rm -rf /home/umeko/.cache/mesa_shader_cache* 2>/dev/null
rm -f /home/umeko/*.rd
timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json TU_DEBUG=sysmem,rd TU5XX_TRACE=1 MESA_SHADER_CACHE_DISABLE=true ./tu5xx/vktriangle > /tmp/run17.log 2>&1
echo "exit=$?"
grep -E 'readback|verify|fill buffer|clean exit' /tmp/run17.log
grep -E 'atrr|sp_(fs|vs)|spirv|VPC|vpc|vfd|VFD' /tmp/run17.log | head -40
sleep 8
echo "== dmesg =="
dmesg | grep -E 'fault|opcode|protected|hangcheck' | tail -6
echo "dmesg lines: $(dmesg | wc -l)"
ls -la /home/umeko/*.rd 2>/dev/null
echo ===DONE===