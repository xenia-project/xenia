#!/usr/bin/env python3

# Copyright 2015 Ben Vanik. All Rights Reserved.

"""Premake trampoline script.
"""

__author__ = "ben.vanik@gmail.com (Ben Vanik)"


from json import loads as jsonloads
import os
from shutil import rmtree
import subprocess
import sys


self_path = os.path.dirname(os.path.abspath(__file__))
root_path = os.path.join(self_path, "..", "..")
premake_submodule_path = os.path.join(root_path, "third_party", "premake-core")
premake_path = premake_submodule_path


def setup_premake_path_override():
    global premake_path
    premake_path = premake_submodule_path
    if sys.platform == "linux":
        # On Android, the repository may be cloned to the external storage, which
        # doesn't support executables in it.
        # In this case, premake-core needs to be checked out in the internal
        # storage, which supports executables, with all the permissions as set in
        # its repository.
        # On Termux, the home directory is in the internal storage - use it for
        # executing.
        # If xenia-build.py doesn't have execute permissions, Xenia is in the external
        # storage now.
        try:
            popen = subprocess.Popen(
                ["uname", "-o"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                text=True)
            if popen.communicate()[0] == "Android\n":
                xb_file = os.path.join(root_path, "xenia-build.py")
                if (os.path.isfile(xb_file) and not os.access(xb_file, os.X_OK) and
                    "HOME" in os.environ):
                  premake_path = os.path.join(os.environ["HOME"], ".xenia-build", "premake-core")
        except Exception:
            pass

setup_premake_path_override()


def main():
    # First try the freshly-built premake.
    premake5_bin = os.path.join(premake_path, "bin", "release", "premake5")
    if not has_bin(premake5_bin):
        # No fresh build, so fallback to checked in copy (which we may not have).
        premake5_bin = os.path.join(self_path, "bin", "premake5")
    if not has_bin(premake5_bin):
        # Still no valid binary, so build it.
        print("premake5 executable not found, attempting build...")
        build_premake()
        premake5_bin = os.path.join(premake_path, "bin", "release", "premake5")
    if not has_bin(premake5_bin):
        # Nope, boned.
        print("ERROR: cannot build premake5 executable.")
        sys.exit(1)

    # Ensure the submodule has been checked out.
    if not os.path.exists(os.path.join(premake_path, "scripts", "package.lua")):
        print("third_party/premake-core was not present; run xb setup...")
        sys.exit(1)

    if sys.platform == "win32":
        # Append the executable extension on windows.
        premake5_bin += ".exe"

    return_code = shell_call([
        premake5_bin,
        f"--scripts={premake_path}",
        ] + sys.argv[1:],
        throw_on_error=False)

    sys.exit(return_code)


def build_premake():
    """Builds premake from source.
    """
    # Ensure that on Android, premake-core is in the internal storage.
    clone_premake_to_internal_storage()
    cwd = os.getcwd()
    try:
        os.chdir(premake_path)
        if sys.platform == "darwin":
            subprocess.call([
                "make",
                "-f", "Bootstrap.mak",
                "osx",
                ])
        elif sys.platform == "win32":
            # Grab Visual Studio version and execute shell to set up environment.
            vs_version = import_vs_environment()
            if not vs_version:
                print("ERROR: Visual Studio not found!")
                sys.exit(1)
                return

            subprocess.call([
                "nmake",
                "-f", "Bootstrap.mak",
                "windows",
                ])
        else:
            subprocess.call([
                "make",
                "-f", "Bootstrap.mak",
                "linux",
                ])
    finally:
        os.chdir(cwd)
    pass


def clone_premake_to_internal_storage():
  """Clones premake to the Android internal storage so it can be executed.
  """
  # premake_path is initialized to a value different than premake_submodule_path
  # if running from the Android external storage, and may not exist yet.
  if premake_path == premake_submodule_path:
      return

  # Ensure the submodule has been checked out.
  if not os.path.exists(
        os.path.join(premake_submodule_path, "scripts", "package.lua")):
      print("third_party/premake-core was not present; run xb setup...")
      sys.exit(1)

  # Create or refresh premake-core in the internal storage.
  print("Cloning premake5 to the internal storage...")
  rmtree(premake_path, ignore_errors=True)
  os.makedirs(premake_path)
  shell_call([
      "git",
      "clone",
      "--depth=1",
      premake_submodule_path,
      premake_path,
      ])


def has_bin(bin):
    """Checks whether the given binary is present.
    """
    for path in os.environ["PATH"].split(os.pathsep):
        if sys.platform == "win32":
            exe_file = os.path.join(path, f"{bin}.exe")
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                return True
        else:
            path = path.strip("\"")
            exe_file = os.path.join(path, bin)
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                return True
    return None


def shell_call(command, throw_on_error=True, stdout_path=None, stderr_path=None, shell=False):
    """Executes a shell command.

    Args:
      command: Command to execute, as a list of parameters.
      throw_on_error: Whether to throw an error or return the status code.
      stdout_path: File path to write stdout output to.
      stderr_path: File path to write stderr output to.

    Returns:
      If throw_on_error is False the status code of the call will be returned.
    """
    stdout_file = None
    if stdout_path:
        stdout_file = open(stdout_path, "w")
    stderr_file = None
    if stderr_path:
        stderr_file = open(stderr_path, "w")
    result = 0
    try:
        if throw_on_error:
            result = 1
            subprocess.check_call(command, shell=shell, stdout=stdout_file, stderr=stderr_file)
            result = 0
        else:
            result = subprocess.call(command, shell=shell, stdout=stdout_file, stderr=stderr_file)
    finally:
        if stdout_file:
            stdout_file.close()
        if stderr_file:
            stderr_file.close()
    return result


def import_vs_environment():
    """Finds the installed Visual Studio version and imports
    interesting environment variables into os.environ.

    Returns:
      A version such as 2022 or None if no installation is found.
    """

    if sys.platform != "win32":
        return None

    version = None
    install_path = None
    env_tool_args = None

    vswhere = subprocess.check_output(
        "tools/vswhere/vswhere.exe -version \"[17,)\" -latest -prerelease -format json -utf8 -products"
        " Microsoft.VisualStudio.Product.Enterprise"
        " Microsoft.VisualStudio.Product.Professional"
        " Microsoft.VisualStudio.Product.Community"
        " Microsoft.VisualStudio.Product.BuildTools",
        encoding="utf-8",
    )
    if vswhere:
        vswhere = jsonloads(vswhere)
    if vswhere and len(vswhere) > 0:
        version = int(vswhere[0].get("catalog", {}).get("productLineVersion", 2022))
        install_path = vswhere[0].get("installationPath", None)

    vsdevcmd_path = os.path.join(install_path, "Common7", "Tools", "VsDevCmd.bat")
    if os.access(vsdevcmd_path, os.X_OK):
        env_tool_args = [vsdevcmd_path, "-arch=amd64", "-host_arch=amd64", "&&", "set"]
    else:
        vcvars_path = os.path.join(install_path, "VC", "Auxiliary", "Build", "vcvarsall.bat")
        env_tool_args = [vcvars_path, "x64", "&&", "set"]

    if not version:
        return None

    import_subprocess_environment(env_tool_args)
    os.environ["VSVERSION"] = f"{version}"
    return version


def import_subprocess_environment(args):
    popen = subprocess.Popen(
        args, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    variables, _ = popen.communicate()
    envvars_to_save = (
        "devenvdir",
        "include",
        "lib",
        "libpath",
        "path",
        "pathext",
        "systemroot",
        "temp",
        "tmp",
        "vcinstalldir",
        "windowssdkdir",
        )
    for line in variables.splitlines():
        for envvar in envvars_to_save:
            if f"{envvar}=" in line.lower():
                var, setting = line.split("=", 1)
                if envvar == "path":
                    setting = f"{os.path.dirname(sys.executable)}{os.pathsep}{setting}"
                os.environ[var.upper()] = setting
                break

if __name__ == "__main__":
    main()
