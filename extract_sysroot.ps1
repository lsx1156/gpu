$ErrorActionPreference = 'Continue'
$log = 'd:\project\gpu\extract.log'
"start $(Get-Date)" | Out-File $log
Remove-Item -Recurse -Force d:\project\gpu\sysroot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path d:\project\gpu\sysroot | Out-Null
& C:\Windows\System32\tar.exe -xzf d:\project\gpu\backup\sysroot.tar.gz -C d:\project\gpu\sysroot 2>&1 | Select-Object -Last 8 | Out-File $log -Append
"tar exit: $LASTEXITCODE" | Out-File $log -Append
$so = Get-ChildItem d:\project\gpu\sysroot\usr\lib\aarch64-linux-gnu\*.so -ErrorAction SilentlyContinue
"unversioned .so count: $($so.Count)" | Out-File $log -Append
"done $(Get-Date)" | Out-File $log -Append
