# M1.1 增量编译（复用 build-mesa.ps1 的环境，仅跑 ninja）
param()
$Root = 'D:\project\gpu'
$Sys  = "$Root\sysroot"
$Bld  = "$Root\work\mesa-build"

$env:PKG_CONFIG_LIBDIR = "$Sys\usr\lib\aarch64-linux-gnu\pkgconfig;$Sys\usr\share\pkgconfig"
Remove-Item Env:\PKG_CONFIG_SYSROOT_DIR -ErrorAction SilentlyContinue
$env:PKG_CONFIG_ALLOW_SYSTEM_CFLAGS = '1'
$env:LIBRARY_PATH = "$Sys\usr\lib\aarch64-linux-gnu"
$env:Path = "$Root\tools\msys64\usr\bin;$Root\tools\msys64\ucrt64\bin;" + $env:Path

ninja -C $Bld
Write-Host "incremental build exit=$LASTEXITCODE"