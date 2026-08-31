# Mesa 24.0.5 aarch64 交叉构建（M1.0）一键封装
# 用法: powershell -ExecutionPolicy Bypass -File mesa-cross\build-mesa.ps1 [setup|reconf|build]
param([string]$Action = 'setup')
$Root = 'D:\project\gpu'
$Src  = "$Root\work\mesa-src\mesa-24.0.5"
$Bld  = "$Root\work\mesa-build"
$Sys  = "$Root\sysroot"

$env:PKG_CONFIG_LIBDIR = "$Sys\usr\lib\aarch64-linux-gnu\pkgconfig;$Sys\usr\share\pkgconfig"
# 去掉 sysroot_dir, 靠编译器 --sysroot 自动解析, 避免双前缀
Remove-Item Env:\PKG_CONFIG_SYSROOT_DIR -ErrorAction SilentlyContinue
$env:PKG_CONFIG_ALLOW_SYSTEM_CFLAGS = '1'
# 让交叉 gcc 在 Meson find_library(-latomic 等) 时也能搜到设备多架构库目录
$env:LIBRARY_PATH = "$Sys\usr\lib\aarch64-linux-gnu"
# host 侧工具: MSYS2 的 flex/bison(生成 GLSL 词法/语法分析器), pkgconf 已有
$env:Path = "$Root\tools\msys64\usr\bin;$Root\tools\msys64\ucrt64\bin;" + $env:Path

$opts = @(
  "--cross-file=$Root\mesa-cross\cross-aarch64.ini",
  '-Dplatforms=', '-Degl=disabled', '-Dglx=disabled', '-Dgbm=disabled',
  '-Dgallium-drivers=freedreno', '-Dvulkan-drivers=freedreno',
  '-Dllvm=disabled', '-Dvideo-codecs=',
  '-Dopengl=false', '-Dgles1=disabled', '-Dgles2=disabled',
  '-Dbuild-tests=false',
  '--wrap-mode=nodownload'
)

function Setup { meson setup $Bld $Src @opts }
function Reconf { meson configure $opts $Bld }
function Build { ninja -C $Bld }

switch ($Action) {
  'setup'  { Setup }
  'reconf' { Reconf }
  'build'  { Build }
  default  { Setup; Build }
}
Write-Host "M1.0 Action[$Action] done, exit=$LASTEXITCODE"