import os


def is_executable(path) -> bool:
    return os.path.isfile(path) and os.access(path, os.X_OK)


def get_bin(binary):
    """Checks whether the given binary is present and returns the path.

    Args:
      binary: binary name (without .exe, etc).

    Returns:
      Full path to the binary or None if not found.
    """
    for path in os.environ['PATH'].split(os.pathsep):
        path = path.strip('"')
        exe_file = os.path.join(path, binary)
        if is_executable(exe_file):
            return exe_file
        if is_executable(exe_file + '.exe'):
            return exe_file + '.exe'
    return None


def has_bin(binary):
    """Checks whether the given binary is present in the PATH.

    Args:
      binary: binary name (without .exe, etc).

    Returns:
      True if the binary exists.
    """
    return get_bin(binary) is not None
