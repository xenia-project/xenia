# Ignore HighResolutionTimer custom event
handle SIG34 nostop noprint
# Ignore PosixTimer custom event
handle SIG35 nostop noprint
# Ignore PosixThread exit event
handle SIG32 nostop noprint
# Ignore PosixThread suspend event
handle SIG36 nostop noprint
# Ignore PosixThread user callback event
handle SIG37 nostop noprint
# Ignore Threaded Stack Walker Capture event
handle SIGUSR1 nostop noprint
