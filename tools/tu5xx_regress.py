#!/usr/bin/env python3
"""tu5xx 回归套件：在设备上跑既有探针，按期望表判定 PASS/FAIL。

用法（Windows/MSYS，主机侧）:
  python tools/tu5xx_regress.py --pwfile C:/Users/<u>/AppData/Local/hermes/cache/scratch/umeko.pw
  python tools/tu5xx_regress.py --case vk_tex          # 只跑一个
  python tools/tu5xx_regress.py --mutate             # 自检：故意改坏期望，必须报 FAIL
  python tools/tu5xx_regress.py --list

凭据不入命令行、不打印：只把 --pwfile 的路径交给 plink/pscp（必须是 Windows 路径，
原生 exe 不认 MSYS 的 /tmp）。

判定口径与 work/ROADMAP.md 的 M2.0 一致：每种探针有可判定的像素级判据
（色块颜色+计数、nonbg 计数、坐标取样 RGBA），不接受“看起来对”。
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXPECT_DEFAULT = os.path.join(HERE, "tu5xx_expect.json")

PLINK = r"C:\Program Files\PuTTY\plink.exe"
HOSTKEY = "SHA256:Nl5kB6S14ethuEOsengQBDPnUks5T4nW6fCXW/AstnI"  # 已钉住的 host key

REMOTE_ENV = ("export VK_ICD_FILENAMES=/home/umeko/tu5xx/freedreno_icd.device.json "
              "TU_DEBUG=sysmem MESA_SHADER_CACHE_DISABLE=true; ")

RE_BLOCK = re.compile(r"RGB\(\s*(\d+),\s*(\d+),\s*(\d+)\)\s*x\s*(\d+)")
RE_RGBA = re.compile(r"RGBA?\(\s*(\d+),\s*(\d+),\s*(\d+)(?:,\s*(\d+))?\)")


def run_remote(pwfile, host, remote_cmd, timeout):
    cmd = [PLINK, "-ssh", "-batch", "-pwfile", pwfile, "-hostkey", HOSTKEY,
           host, remote_cmd]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                       errors="replace")
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def check_blocks(out, exp):
    """比对纹理探针的色块集合：{(r,g,b): count} 必须与期望完全一致。"""
    got = {}
    for m in RE_BLOCK.finditer(out):
        rgb = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        got[rgb] = got.get(rgb, 0) + int(m.group(4))
    want = {(tuple(e["rgb"])): e["count"] for e in exp}
    errs = []
    for rgb, cnt in want.items():
        if rgb not in got:
            errs.append("缺色块 RGB%s" % (rgb,))
        elif got[rgb] != cnt:
            errs.append("色块 RGB%s 计数 %d != 期望 %d" % (rgb, got[rgb], cnt))
    for rgb in got:
        if rgb not in want:
            errs.append("多出色块 RGB%s x %d" % (rgb, got[rgb]))
    return errs


def check_nonbg(out, marker, value):
    """匹配 'att1: nonbg=20808' 或 'nonbg=20808' 形态。"""
    pat = re.escape(marker) + r":?\s*nonbg=(\d+)" if marker else r"nonbg=(\d+)"
    m = re.search(pat, out)
    if not m:
        return ["未找到 nonbg 计数 (marker=%r)" % marker]
    if int(m.group(1)) != value:
        return ["nonbg=%s != 期望 %d (marker=%r)" % (m.group(1), value, marker)]
    return []


def check_rgba(out, marker, rgba, tol):
    """在含 marker 的行上取第一个 RGBA(...) 并带容差比对。"""
    for line in out.splitlines():
        if marker in line:
            m = RE_RGBA.search(line)
            if not m:
                return ["行内有 marker 但没有 RGBA: %s" % line.strip()]
            g = m.groups()
            got = (int(g[0]), int(g[1]), int(g[2]), int(g[3]) if g[3] is not None else 255)
            if any(abs(g - w) > tol for g, w in zip(got, rgba)):
                return ["%s: RGBA%s != 期望 %s (tol=%d)" %
                        (marker, got, tuple(rgba), tol)]
            return []
    return ["未找到 marker=%r 的行" % marker]


def check_regex(out, pattern):
    return [] if re.search(pattern, out) else ["缺少模式 /%s/" % pattern]


def evaluate(case, out):
    errs = []
    for chk in case.get("checks", []):
        kind = chk["kind"]
        if kind == "blocks":
            errs += check_blocks(out, chk["expect"])
        elif kind == "nonbg":
            errs += check_nonbg(out, chk.get("marker"), chk["value"])
        elif kind == "rgba":
            errs += check_rgba(out, chk["marker"], chk["rgba"], chk.get("tol", 2))
        elif kind == "regex":
            errs += check_regex(out, chk["pattern"])
        else:
            errs.append("未知检查类型 %r" % kind)
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pwfile", required=False, help="plink -pwfile (Windows 路径)")
    ap.add_argument("--host", default="umeko@192.168.1.176")
    ap.add_argument("--expect", default=EXPECT_DEFAULT)
    ap.add_argument("--case", action="append", help="只跑指定用例（可多次）")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--mutate", action="store_true",
                    help="自检：把第一个 blocks 用例的首个计数 +1，要求判定 FAIL")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    with open(args.expect, encoding="utf-8") as f:
        cfg = json.load(f)
    cases = cfg["cases"]
    if args.case:
        cases = [c for c in cases if c["name"] in args.case]
        if not cases:
            print("没有匹配的用例"); return 2
    if args.list:
        for c in cases:
            print("%-14s %s/%s %s" % (c["name"], c["dir"], c["exe"], " ".join(c.get("args", []))))
        return 0

    if args.mutate:
        for c in cases:
            for chk in c.get("checks", []):
                if chk["kind"] == "blocks":
                    chk["expect"][0]["count"] += 1
                    print("[mutate] %s: 期望计数 +1 -> 应当 FAIL" % c["name"])
                    break
            else:
                continue
            break

    if not args.pwfile:
        print("需要 --pwfile（plink 的 -pwfile，Windows 路径）"); return 2
    if not os.path.exists(args.pwfile):
        print("pwfile 不存在: %s" % args.pwfile); return 2

    fail = 0
    for c in cases:
        remote = (REMOTE_ENV + "cd %s && timeout %d ./%s %s 2>&1"
                  % (c["dir"], args.timeout, c["exe"], " ".join(c.get("args", []))))
        try:
            rc, out = run_remote(args.pwfile, args.host, remote, args.timeout + 30)
        except subprocess.TimeoutExpired:
            print("[FAIL] %-14s 超时" % c["name"]); fail += 1; continue
        errs = evaluate(c, out)
        if rc != 0:
            errs.insert(0, "退出码 %d" % rc)
        if errs:
            fail += 1
            print("[FAIL] %-14s %s" % (c["name"], "; ".join(errs[:4])))
            tail = "\n".join(out.strip().splitlines()[-6:])
            for ln in tail.splitlines():
                print("         | %s" % ln)
        else:
            print("[PASS] %-14s %d 项检查" % (c["name"], len(c.get("checks", []))))

    print("\n%d 通过 / %d 失败 (共 %d)" % (len(cases) - fail, fail, len(cases)))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
