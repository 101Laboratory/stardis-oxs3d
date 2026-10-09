# 自动请求管理员权限
if (-not ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent() `
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
{
    Start-Process powershell `
        "-ExecutionPolicy Bypass -File `"$PSCommandPath`"" `
        -Verb RunAs
    exit
}


$env:STARDIS_POOL_SIZE="32768"

# Nsight Compute single kernel profiling
ncu --set full --kernel-name "trace_rays_topk_kernel" `
    --launch-count 5 --launch-skip 10 --kill yes`
    ..\..\stardis-cus3d\build\bin\Release\stardis.exe `
    -M porous.txt -t 4 -V 3 -R spp=8:img=320x320:fov=30.0:pos=0.05,0.01,0.0:tgt=0.0,0.0,0.0:up=0.0,0.0,1.0