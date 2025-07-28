$pythonExecutables = "python", "python3", "py"
foreach ($pythonExecutable in $pythonExecutables) {
    if (!$pythonPath) {
        $pythonPath = powershell -NoLogo -NonInteractive "(Get-Command -ErrorAction SilentlyContinue $pythonexecutable).Definition" # Hack to not give command suggestion
        if ($pythonPath) {
            break
        }
    }
}
# Neither found, error and exit
if (!$pythonPath) {
    Throw "ERROR: Python 3.9+ 64-bit must be installed and on PATH:`nhttps://www.python.org/"
}

& $pythonPath "$($PSScriptRoot)/xenia-build" $args
