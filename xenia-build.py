#!/usr/bin/env python

# Copyright 2013 Ben Vanik. All Rights Reserved.

"""
"""

__author__ = 'ben.vanik@gmail.com (Ben Vanik)'


import os
import re
import shutil
import subprocess
import sys


def main():
  # Add self to the root search path.
  sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

  # Augment path to include our fancy things.
  os.environ['PATH'] += os.pathsep + os.pathsep.join([
      os.path.abspath('third_party/ninja/'),
      os.path.abspath('third_party/gyp/')
      ])

  # Check python version.
  if not sys.version_info[:2] == (2, 7):
    print('ERROR: python 2.7 required')
    print('(unfortunately gyp doesn\'t work with 3!)')
    sys.exit(1)
    return

  # Grab Visual Studio version and execute shell to set up environment.
  if sys.platform == 'win32':
    vs_version = import_vs_environment()
    if not vs_version == 2013:
      print('ERROR: Visual Studio 2013 not found!')
      print('Ensure you have the VS120COMNTOOLS environment variable!')
      sys.exit(1)
      return

  # Grab all commands.
  commands = discover_commands()

  # Parse command name and dispatch.
  try:
    if len(sys.argv) < 2:
      raise ValueError('No command given')
    command_name = sys.argv[1]
    if not command_name in commands:
      raise ValueError('Command "%s" not found' % (command_name))

    command = commands[command_name]
    return_code = run_command(command=command,
                              args=sys.argv[2:],
                              cwd=os.getcwd())
  except ValueError:
    print(usage(commands))
    return_code = 1
  except Exception as e:
    #print e
    raise
    return_code = 1
  sys.exit(return_code)


def import_vs_environment():
  """Finds the installed Visual Studio version and imports
  interesting environment variables into os.environ.

  Returns:
    A version such as 2010, 2012, or 2013 or None if no VS is found.
  """
  version = 0
  tools_path = ''
  if 'VS120COMNTOOLS' in os.environ:
    version = 2013
    tools_path = os.environ['VS120COMNTOOLS']
  elif 'VS110COMNTOOLS' in os.environ:
    version = 2012
    tools_path = os.environ['VS110COMNTOOLS']
  elif 'VS100COMNTOOLS' in os.environ:
    version = 2010
    tools_path = os.environ['VS100COMNTOOLS']
  if version == 0:
    return None
  tools_path = os.path.join(tools_path, '..\\..\\vc\\vcvarsall.bat')

  args = [tools_path, '&&', 'set']
  popen = subprocess.Popen(
      args, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  variables, _ = popen.communicate()
  envvars_to_save = (
      'devenvdir',
      'include',
      'lib',
      'libpath',
      'path',
      'pathext',
      'systemroot',
      'temp',
      'tmp',
      'windowssdkdir',
      )
  for line in variables.splitlines():
    for envvar in envvars_to_save:
      if re.match(envvar + '=', line.lower()):
        var, setting = line.split('=', 1)
        if envvar == 'path':
          setting = os.path.dirname(sys.executable) + os.pathsep + setting
        os.environ[var.upper()] = setting
        break

  os.environ['VSVERSION'] = str(version)
  return version


def discover_commands():
  """Looks for all commands and returns a dictionary of them.
  In the future commands could be discovered on disk.

  Returns:
    A dictionary containing name-to-Command mappings.
  """
  commands = {
      'setup': SetupCommand(),
      'pull': PullCommand(),
      'gyp': GypCommand(),
      'build': BuildCommand(),
      'test': TestCommand(),
      'clean': CleanCommand(),
      'nuke': NukeCommand(),
      }
  return commands


def usage(commands):
  """Gets usage info that can be displayed to the user.

  Args:
    commands: A command dictionary from discover_commands.

  Returns:
    A string containing usage info and a command listing.
  """
  s = 'xenia-build.py command [--help]\n'
  s += '\n'
  s += 'Commands:\n'
  command_names = sorted(commands.keys())
  for command_name in command_names:
    s += '  %s\n' % (command_name)
    command_help = commands[command_name].help_short
    if command_help:
      s += '    %s\n' % (command_help)
  return s


def run_command(command, args, cwd):
  """Runs a command with the given context.

  Args:
    command: Command to run.
    args: Arguments, with the app and command name stripped.
    cwd: Current working directory.

  Returns:
    0 if the command succeeded and non-zero otherwise.

  Raises:
    ValueError: The command could not be found or was not specified.
  """
  # TODO(benvanik): parse arguments/etc.
  return command.execute(args, cwd)


def has_bin(bin):
  """Checks whether the given binary is present.
  """
  for path in os.environ["PATH"].split(os.pathsep):
    path = path.strip('"')
    exe_file = os.path.join(path, bin)
    if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
      return True
    exe_file = exe_file + '.exe'
    if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
      return True
  return None


def shell_call(command, throw_on_error=True):
  """Executes a shell command.

  Args:
    command: Command to execute.
    throw_on_error: Whether to throw an error or return the status code.

  Returns:
    If throw_on_error is False the status code of the call will be returned.
  """
  if throw_on_error:
    subprocess.check_call(command, shell=True)
    return 0
  else:
    return subprocess.call(command, shell=True)


class Command(object):
  """Base type for commands.
  """

  def __init__(self, name, help_short=None, help_long=None, *args, **kwargs):
    """Initializes a command.

    Args:
      name: The name of the command exposed to the management script.
      help_short: Help text printed alongside the command when queried.
      help_long: Extended help text when viewing command help.
    """
    self.name = name
    self.help_short = help_short
    self.help_long = help_long

  def execute(self, args, cwd):
    """Executes the command.

    Args:
      args: Arguments list.
      cwd: Current working directory.

    Returns:
      Return code of the command.
    """
    return 1


def post_update_deps(config):
  """Runs common tasks that should be executed after any deps are changed.

  Args:
    config: 'debug' or 'release'.
  """
  pass


class SetupCommand(Command):
  """'setup' command."""

  def __init__(self, *args, **kwargs):
    super(SetupCommand, self).__init__(
        name='setup',
        help_short='Setup the build environment.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Setting up the build environment...')
    print('')

    # Setup submodules.
    print('- git submodule init / update...')
    shell_call('git submodule init')
    shell_call('git submodule update')
    print('')

    # Disable core.filemode on Windows to prevent weird file mode diffs in git.
    # TODO(benvanik): check cygwin test - may be wrong when using Windows python
    if os.path.exists('/Cygwin.bat'):
      print('- setting filemode off on cygwin...')
      shell_call('git config core.filemode false')
      shell_call('git submodule foreach git config core.filemode false')
      print('')

    # Run the ninja bootstrap to build it, if it's missing.
    if (not os.path.exists('third_party/ninja/ninja') and
       not os.path.exists('third_party/ninja/ninja.exe')):
      print('- preparing ninja...')
      # Windows needs --x64 to force building the 64-bit ninja.
      extra_args = ''
      #if sys.platform == 'win32':
      #  extra_args = '--x64'
      shell_call('python third_party/ninja/bootstrap.py ' + extra_args)
      print('')

    # Binutils.
    # TODO(benvanik): disable on Windows
    print('- binutils...')
    if True:
      print('binutils disabled')
    elif sys.platform == 'win32':
      print('WARNING: ignoring binutils on Windows... don\'t change tests!')
    else:
      if not os.path.exists('build/binutils'):
        os.makedirs('build/binutils')
      os.chdir('build/binutils')
      shell_call(' '.join([
          '../../third_party/binutils/configure',
          '--disable-debug',
          '--disable-dependency-tracking',
          '--disable-werror',
          '--enable-interwork',
          '--enable-multilib',
          '--target=powerpc-none-elf',
          '--with-gnu-ld',
          '--with-gnu-as',
          ]))
      shell_call('make')
      os.chdir(cwd)
    print('')

    post_update_deps('debug')
    post_update_deps('release')

    print('- running gyp...')
    run_all_gyps()
    print('')

    print('Success!')
    return 0


class PullCommand(Command):
  """'pull' command."""

  def __init__(self, *args, **kwargs):
    super(PullCommand, self).__init__(
        name='pull',
        help_short='Pulls the repo and all dependencies.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Pulling...')
    print('')

    print('- pulling self...')
    shell_call('git pull')
    print('')

    print('- pulling dependencies...')
    shell_call('git submodule update')
    print('')

    post_update_deps('debug')
    post_update_deps('release')

    print('- running gyp...')
    run_all_gyps()
    print('')

    print('Success!')
    return 0


def run_gyp(format):
  """Runs gyp on the main project with the given format.

  Args:
    format: gyp -f value.
  """
  shell_call(' '.join([
      'gyp',
      '-f %s' % (format),
      # Removes the out/ from ninja builds.
      '-G output_dir=.',
      '--depth=.',
      '--toplevel-dir=.',
      '--generator-output=build/xenia/',
      # Set the VS version.
      '-G msvs_version=%s' % (os.environ.get('VSVERSION', 2013)),
      #'-D windows_sdk_dir=%s' % (os.environ['WINDOWSSDKDIR']),
      '-D windows_sdk_dir="C:\\Program Files (x86)\\Windows Kits\\8.1"',
      'xenia.gyp',
      ]))


def run_all_gyps():
  """Runs all gyp configurations.
  """
  run_gyp('ninja')
  if sys.platform == 'darwin':
    run_gyp('xcode')
  elif sys.platform == 'win32':
    run_gyp('msvs')


class GypCommand(Command):
  """'gyp' command."""

  def __init__(self, *args, **kwargs):
    super(GypCommand, self).__init__(
        name='gyp',
        help_short='Runs gyp to update all projects.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Running gyp...')
    print('')

    # Update GYP.
    run_all_gyps()

    print('Success!')
    return 0


class BuildCommand(Command):
  """'build' command."""

  def __init__(self, *args, **kwargs):
    super(BuildCommand, self).__init__(
        name='build',
        help_short='Builds the project.',
        *args, **kwargs)

  def execute(self, args, cwd):
    # TODO(benvanik): add arguments:
    # --force
    debug = '--debug' in args
    config = 'Debug' if debug else 'Release'

    print('Building %s...' % (config))
    print('')

    print('- running gyp for ninja...')
    run_gyp('ninja')
    print('')

    print('- building xenia in %s...' % (config))
    result = shell_call('ninja -C build/xenia/%s' % (config),
                        throw_on_error=False)
    print('')
    if result != 0:
      return result

    print('Success!')
    return 0


class TestCommand(Command):
  """'test' command."""

  def __init__(self, *args, **kwargs):
    super(TestCommand, self).__init__(
        name='test',
        help_short='Runs all tests.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Testing...')
    print('')

    # First run make and update all of the test files.
    # TOOD(benvanik): disable on Windows
    print('Updating test files...')
    result = shell_call('make -C test/codegen/')
    print('')
    if result != 0:
      return result

    # Start the test runner.
    print('Launching test runner...')
    result = shell_call('bin/xenia-test')
    print('')

    return result


class CleanCommand(Command):
  """'clean' command."""

  def __init__(self, *args, **kwargs):
    super(CleanCommand, self).__init__(
        name='clean',
        help_short='Removes intermediate files and build output.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Cleaning build artifacts...')
    print('')

    print('- removing build/xenia/...')
    if os.path.isdir('build/xenia/'):
      shutil.rmtree('build/xenia/')
    print('')

    print('Success!')
    return 0


class NukeCommand(Command):
  """'nuke' command."""

  def __init__(self, *args, **kwargs):
    super(NukeCommand, self).__init__(
        name='nuke',
        help_short='Removes all build/ output.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print('Cleaning build artifacts...')
    print('')

    print('- removing build/...')
    if os.path.isdir('build/'):
      shutil.rmtree('build/')
    print('')

    print('Success!')
    return 0


if __name__ == '__main__':
  main()
