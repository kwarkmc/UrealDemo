$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$Action = 'process-check'
$python = Join-Path $projectRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Create the project .venv before this check.' }

# Load the exact launcher function without executing dev.ps1's build/start actions.
$tokens = $null
$parseErrors = $null
$tree = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'scripts\dev.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'dev.ps1 syntax errors' }
$function = $tree.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-DevProcess'
}, $true)
. ([scriptblock]::Create($function.Extent.Text))

$outputPath = [System.IO.Path]::GetTempFileName()
try {
    $expected = @('', 'plain', 'space here', 'C:\folder with spaces\', 'a"b', 'a\\"b',
                  '$(not evaluated)', '`literal`', '&|<>%', ([string][char]0xD55C))
    $code = 'import json,pathlib,sys; pathlib.Path(sys.argv[1]).write_text(json.dumps(sys.argv[2:])); sys.exit(7)'
    $exitCode = Invoke-DevProcess $python (@('-c', $code, $outputPath) + $expected)
    if ($exitCode -ne 7) { throw "Exit code lost: $exitCode" }
    $received = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    if ($received.Count -ne $expected.Count) { throw "Argument count changed: $(Get-Content -LiteralPath $outputPath -Raw)" }
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if ($received[$index] -cne $expected[$index]) { throw "Argument $index changed" }
    }
    # Exercise redirected stderr and the existing raw-command branch as well.
    $exitCode = Invoke-DevProcess $python @('-c', 'import sys; sys.stderr.write("stderr-check\n"); sys.exit(3)')
    if ($exitCode -ne 3) { throw 'stderr/exit handling changed' }
    $exitCode = Invoke-DevProcess -File $env:ComSpec -RawArguments '/d /c exit 4'
    if ($exitCode -ne 4) { throw 'Raw command exit code changed' }
    Write-Output "PASS: PowerShell $($PSVersionTable.PSVersion), argv round-trip, stderr and exit codes"
}
finally {
    Remove-Item -LiteralPath $outputPath -ErrorAction SilentlyContinue
}
