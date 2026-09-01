#!/bin/sh
cd /home/umeko || exit 1
rm -rf /home/umeko/.cache/mesa_shader_cache* 2>/dev/null
rm -f /tmp/tu_now.log
timeout 60 env VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json \
  TU_DEBUG=sysmem,nir IR3_SHADER_DEBUG=disasm MESA_SHADER_CACHE_DISABLE=true \
  ./tu5xx/vktriangle >> /tmp/tu_now.log 2>&1
echo "exit=$?"
echo "lines: $(wc -l < /tmp/tu_now.log)"
echo "== tail =="
tail -20 /tmp/tu_now.log
echo "== readback =="
grep -E 'readback|probe|center|center=|=20808|nonbg' /tmp/tu_now.log | head
echo ===DONE===