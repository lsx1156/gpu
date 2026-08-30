# 交叉编译一键封装：x86_64 PC -> aarch64 Linux (mido / Ubuntu 24.04)
# 用法:
#   .\cross.ps1 tests\hello.c                     # C, 输出 hello
#   .\cross.ps1 tests\hello.cpp -Cpp              # C++ (静态 libstdc++)
#   .\cross.ps1 src\foo.c -o out\foo -O2 -lEGL    # 透传任意编译参数
# 依赖: tools\(ARM GNU Toolchain 15.2.Rel1) + sysroot\(设备 Ubuntu 24.04 arm64)

param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Src,
    [string]$Out,
    [switch]$Cpp,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$S    = "$Root/sysroot"
$I    = "$S/usr/include/aarch64-linux-gnu"
$Bin  = Join-Path $Root 'tools\bin'

# --- sysroot 关键修复的自动重建（junction 打包/复制后会失效） ---
if (-not (Test-Path "$Root\slib")) {
    New-Item -ItemType Junction -Path "$Root\slib" -Target "$S\usr\lib\aarch64-linux-gnu" | Out-Null
}
if (-not (Test-Path "$S\lib")) {
    New-Item -ItemType Junction -Path "$S\lib" -Target "$S\usr\lib" | Out-Null
}
foreach ($c in 'crt1.o', 'Scrt1.o', 'crti.o', 'crtn.o', 'Mcrt1.o', 'ld-linux-aarch64.so.1') {
    $dst = "$S\usr\lib\$c"
    if (-not (Test-Path $dst)) { Copy-Item "$S\usr\lib\aarch64-linux-gnu\$c" $dst }
}

if (-not $Out) { $Out = [IO.Path]::GetFileNameWithoutExtension($Src) }

$argv = @("--sysroot=$S", "-isystem", $I, "-L", "$Root/slib")
if ($Cpp) {
    & "$Bin\aarch64-none-linux-gnu-g++.exe" @argv -static-libstdc++ $Src -o $Out @Rest
} else {
    & "$Bin\aarch64-none-linux-gnu-gcc.exe" @argv $Src -o $Out @Rest
}
exit $LASTEXITCODE
