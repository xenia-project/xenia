function Write-FatalError($message) {
    [Console]::ForegroundColor = 'red'
    [Console]::Error.WriteLine($message)
    [Console]::ResetColor()
    Exit 1
}

$pythonPath = Get-Command python | Select-Object -ExpandProperty Definition

# Check for 'python3' if 'python' isn't found
if ([string]::IsNullOrEmpty($pythonPath)) {
    $pythonPath = Get-Command python3 | Select-Object -ExpandProperty Definition
}
# Neither found, error and exit
if ([string]::IsNullOrEmpty($pythonPath)) {
    Write-FatalError "ERROR: no Python executable found on PATH.`nMake sure you can run 'python' or 'python3' in a Command Prompt."
}

python -c "import sys; sys.exit(1 if not sys.version_info[:2] >= (3, 4) else 0)"
if ($LASTEXITCODE -gt 0) {
    Write-FatalError "ERROR: Python version mismatch, not at least 3.4.`nFound Python executable was $($pythonPath)"
}

python "$($PSScriptRoot)/xenia-build" $args