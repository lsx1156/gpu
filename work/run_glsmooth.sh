#!/bin/sh
# run_glsmooth: 抓最小 fd5 GL smooth-varying 参照 RD
cd /tmp/glsmoothref 2>/dev/null || { mkdir -p /tmp/glsmoothref; cd /tmp/glsmoothref; }
rm -f *.rd
export DISPLAY=:0
sh -c "cd /tmp/glsmoothref && FD_MESA_DEBUG=rd ./glsmooth > run.log 2>&1"
echo "exit=$?"
echo "== out =="
grep -E 'link|CENTER|GLSMOOTH_DONE|FAIL' run.log
echo "== rd files =="
ls -la *.rd 2>/dev/null
echo "== driver/log head =="
head -30 run.log
echo ===DONE===