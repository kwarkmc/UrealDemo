param(
    [ValidateSet('check', 'build', 'editor', 'setup-scene', 'demo', 'api')]
    [string]$Action = 'check',
    [string]$UnrealRoot = $(if ($env:UAVSIM_UNREAL_ROOT) { $env:UAVSIM_UNREAL_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'UAVSim.uproject'

# MSBuild rejects duplicate PATH/Path inherited from this host. Normalize only children.
function Invoke-DevProcess([string]$File, [string[]]$Arguments, [switch]$Background, [string]$RawArguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new($File)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $projectRoot
    $start.RedirectStandardOutput = -not $Background
    $start.RedirectStandardError = -not $Background
    [void]$start.Environment.Remove('PATH')
    [void]$start.Environment.Remove('Path')
    $start.Environment['Path'] = [Environment]::GetEnvironmentVariable('Path')
    if ($RawArguments) { $start.Arguments = $RawArguments }
    elseif ($null -ne $start.ArgumentList) {
        foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
    }
    else {
        # .NET Framework (Windows PowerShell 5.1) needs Windows argv quoting.
        $quoted = foreach ($argument in $Arguments) {
            '"' + ([regex]::Replace($argument, '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
        }
        $start.Arguments = $quoted -join ' '
    }
    $process = [System.Diagnostics.Process]::Start($start)
    if ($Background) { Write-Output "PID: $($process.Id). Action: $Action"; return }
    $errorOutput = $process.StandardError.ReadToEndAsync()
    while (($line = $process.StandardOutput.ReadLine()) -ne $null) { Write-Host $line }
    $process.WaitForExit()
    $errorText = $errorOutput.GetAwaiter().GetResult()
    if ($errorText) { Write-Host $errorText }
    $code = $process.ExitCode
    $process.Dispose()
    return $code
}

if ($Action -eq 'check') {
    $cmakePath = (Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $cmakePath) {
        $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswherePath) {
            $cmakePath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
        }
    }
    if (-not $cmakePath) { throw 'CMake is missing. Install the components in .vsconfig.' }
    $buildPath = Join-Path $projectRoot 'build'
    $code = Invoke-DevProcess $cmakePath @('-S', $projectRoot, '-B', $buildPath, '-A', 'x64')
    if ($code) { exit $code }
    $code = Invoke-DevProcess $cmakePath @('--build', $buildPath, '--config', 'Release')
    if ($code) { exit $code }
    exit (Invoke-DevProcess (Join-Path (Split-Path $cmakePath) 'ctest.exe') @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure'))
}

if ($Action -eq 'api') {
    $pythonPath = Join-Path $projectRoot '.venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $pythonPath)) { throw 'Python environment is missing: .venv' }
    Invoke-DevProcess $pythonPath @('-u', (Join-Path $PSScriptRoot 'serve-api.py'), '--unreal-root', $UnrealRoot) -Background
    return
}

$editorPath = Join-Path $UnrealRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Unreal installation is incomplete or the path is wrong: $UnrealRoot" }
if ($Action -eq 'build') {
    $buildScript = Join-Path $UnrealRoot 'Engine\Build\BatchFiles\Build.bat'
    $buildCommand = '""' + $buildScript + '" UAVSimEditor Win64 Development "-Project=' + $projectPath + '" -WaitMutex -NoHotReloadFromIDE"'
    # cmd.exe parses its own quoting; ArgumentList escaping is for normal executables.
    exit (Invoke-DevProcess -File $env:ComSpec -RawArguments ('/d /s /c ' + $buildCommand))
}

$editorArgs = @($projectPath, '-ModelContextProtocolStartServer', '-ModelContextProtocolPort=8000', '-RCWebControlEnable')
if ($Action -eq 'setup-scene') {
    $editorArgs += '-ExecutePythonScript=' + (Join-Path $PSScriptRoot 'setup-scene.py')
}
if ($Action -eq 'demo') { $editorArgs += @('-game', '-windowed', '-ResX=1600', '-ResY=900') }
Invoke-DevProcess $editorPath $editorArgs -Background
