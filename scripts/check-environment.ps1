param([string]$UnrealRoot = $(if ($env:UAVSIM_UNREAL_ROOT) { $env:UAVSIM_UNREAL_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }))

$ErrorActionPreference = 'Stop'
$compilerPath = (Get-Command cl.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
$cmakePath = (Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (Test-Path -LiteralPath $vswherePath -PathType Leaf) {
    if (-not $compilerPath) {
        $compilerPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' | Select-Object -First 1
    }
    if (-not $cmakePath) {
        $cmakePath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
    }
}

$editorPath = if ($UnrealRoot) { Join-Path $UnrealRoot 'Engine\Binaries\Win64\UnrealEditor.exe' } else { $null }
$missing = $false
foreach ($item in @(
    @{ Name = 'MSVC'; Path = $compilerPath },
    @{ Name = 'CMake'; Path = $cmakePath },
    @{ Name = 'Unreal Editor'; Path = $editorPath }
)) {
    if ($item.Path -and (Test-Path -LiteralPath $item.Path -PathType Leaf)) {
        Write-Output "FOUND $($item.Name): $($item.Path)"
    } else {
        Write-Output "MISSING $($item.Name)"
        $missing = $true
    }
}

if (-not $UnrealRoot) {
    Write-Output 'Set -UnrealRoot or UAVSIM_UNREAL_ROOT to your Unreal installation directory.'
}
Write-Output 'File discovery only. Build compatibility and Unreal MCP connection need separate checks.'
if ($missing) { exit 1 }
exit 0
