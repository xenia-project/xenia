function Write-FatalError($message) {
    [Console]::ForegroundColor = 'red'
    [Console]::Error.WriteLine($message)
    [Console]::ResetColor()
    Exit 1
}

$pythonExecutables = 'python', 'python3'
foreach ($pythonExecutable in $pythonExecutables) {
    if (!$pythonPath) {
        $pythonPath = powershell -NoLogo -NonInteractive "(Get-Command -ErrorAction SilentlyContinue $pythonexecutable).Definition" # Hack to not give command suggestion
    } else {
        break
    }
}
# Neither found, error and exit
$pythonMinimumVer = 3,8
if (!$pythonPath) {
    Write-FatalError "ERROR: Python $($pythonMinimumVer[0]).$($pythonMinimumVer[1])+ must be installed and on PATH:`nhttps://www.python.org/"
}

& $pythonPath -c "import sys; sys.exit(1 if not sys.version_info[:2] >= ($($pythonMinimumVer[0]), $($pythonMinimumVer[1])) else 0)"
if ($LASTEXITCODE -gt 0) {
    Write-FatalError "ERROR: Python version mismatch, not at least $($pythonMinimumVer[0]).$($pythonMinimumVer[1]).`nFound Python executable was `"$($pythonPath)`"."
}

& $pythonPath "$($PSScriptRoot)/xenia-build" $args
