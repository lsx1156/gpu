#!/bin/sh
# run_glsmooth_disasm: 抓 fd5 GL smooth-varying 参照的 IR3 反汇编(FS/VS)
cd /tmp/glsmoothref 2>/dev/null || { mkdir -p /tmp/glsmoothref; cd /tmp/glsmoothref; }
rm -f *.dis
rm -rf /home/umeko/.cache/mesa_shader_cache* 2>/dev/null
export DISPLAY=:0
sh -c "cd /tmp/glsmoothref && env IR3_SHADER_DEBUG=disasm MESA_SHADER_CACHE_DISABLE=true FD_MESA_DEBUG=flush ./glsmooth > run_disasm.log 2>&1"
echo "exit=$?"
echo "== out =="
grep -E 'CENTER|GLSMOOTH_DONE' run_disasm.log
echo "== shader sections =="
grep -nE '^(SHADER|FRAG|VERT|VERTEX|FS: |VS: |        |DEV)|disasm|;.*smooth|ldlv|stl|ldl' run_disasm.log | head -80
echo "== log lines: $(wc -l < run_disasm.log) =="