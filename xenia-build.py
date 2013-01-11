#!/usr/bin/env python

# Copyright 2013 Ben Vanik. All Rights Reserved.

"""
"""

__author__ = 'ben.vanik@gmail.com (Ben Vanik)'


import os
import shutil
import subprocess
import sys


def main():
  # Add self to the root search path.
  sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

  # Check python version.
  if sys.version_info < (2, 7):
    print 'ERROR: python 2.7+ required'
    sys.exit(1)
    return

  # Grab all commands.
  commands = discover_commands()

  # Parse command name and dispatch.
  try:
    if len(sys.argv) < 2:
      raise ValueError('No command given')
    command_name = sys.argv[1]
    if not commands.has_key(command_name):
      raise ValueError('Command "%s" not found' % (command_name))

    command = commands[command_name]
    return_code = run_command(command=command,
                              args=sys.argv[2:],
                              cwd=os.getcwd())
  except ValueError:
    print usage(commands)
    return_code = 1
  except Exception as e:
    #print e
    raise
    return_code = 1
  sys.exit(return_code)


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
  command_names = commands.keys()
  command_names.sort()
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
  DEVNULL = open(os.devnull, 'wb')
  return True if subprocess.call(
      'which %s' % (bin), shell=True, stdout=DEVNULL) == 0 else False


def shell_call(command):
  """Executes a shell command.
  """
  subprocess.check_call(command, shell=True)


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
  print '- building llvm...'
  shell_call('third_party/ninja/ninja -C build/llvm/%s-obj/ install' % (config))
  print ''


class SetupCommand(Command):
  """'setup' command."""

  def __init__(self, *args, **kwargs):
    super(SetupCommand, self).__init__(
        name='setup',
        help_short='Setup the build environment.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print 'Setting up the build environment...'
    print ''

    # Setup submodules.
    print '- git submodule init / update...'
    shell_call('git submodule init')
    shell_call('git submodule update')
    print ''

    # LLVM needs to pull with --rebase.
    # TODO(benvanik): any way to do this to just the submodule?
    #shell_call('git config branch.master.rebase true')

    # Disable core.filemode on Windows to prevent weird file mode diffs in git.
    # TODO(benvanik): check cygwin test - may be wrong when using Windows python
    if os.path.exists('/Cygwin.bat'):
      print '- setting filemode off on cygwin...'
      shell_call('git config core.filemode false')
      shell_call('git submodule foreach git config core.filemode false')
      print ''

    # Run the ninja bootstrap to build it, if it's missing.
    if not os.path.exists('third_party/ninja/ninja'):
      print '- preparing ninja...'
      shell_call('python third_party/ninja/bootstrap.py')
      print ''

    # Ensure cmake is present.
    if not has_bin('cmake'):
      print '- installing cmake...'
      if has_bin('brew'):
        shell_call('brew install cmake')
      elif has_bin('apt-get'):
        shell_call('sudo apt-get install cmake')
      else:
        print 'ERROR: need to install cmake, use:'
        print 'http://www.cmake.org/cmake/resources/software.html'
        return 1
      print ''

    # LLVM.
    print '- preparing llvm...'
    #generator = 'Visual Studio 10 Win64'
    generator = 'Ninja'
    def prepareLLVM(path, obj_path, mode):
      os.chdir(cwd)
      if not os.path.exists(path):
        os.makedirs(path)
      if not os.path.exists(obj_path):
        os.makedirs(obj_path)
      os.chdir(obj_path)
      shell_call(' '.join([
          'PATH=$PATH:../../../third_party/ninja/',
          'cmake',
          '-G"%s"' % (generator),
          '-DCMAKE_INSTALL_PREFIX:STRING=../../../%s' % (path),
          '-DCMAKE_BUILD_TYPE:STRING=%s' % (mode),
          '-DLLVM_TARGETS_TO_BUILD:STRING="X86"',
          '-DLLVM_INCLUDE_EXAMPLES:BOOL=OFF',
          '-DLLVM_INCLUDE_TESTS:BOOL=OFF',
          '../../../third_party/llvm/',
          ]))
      os.chdir(cwd)
    prepareLLVM('build/llvm/debug/', 'build/llvm/debug-obj/',  'Debug')
    prepareLLVM('build/llvm/release/', 'build/llvm/release-obj/', 'Release')
    print ''

    post_update_deps('debug')
    post_update_deps('release')

    print '- running gyp...'
    run_all_gyps()
    print ''

    print 'Success!'
    return 0


class PullCommand(Command):
  """'pull' command."""

  def __init__(self, *args, **kwargs):
    super(PullCommand, self).__init__(
        name='pull',
        help_short='Pulls the repo and all dependencies.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print 'Pulling...'
    print ''

    print '- pulling self...'
    shell_call('git pull')
    print ''

    print '- pulling dependencies...'
    shell_call('git submodule update')
    print ''

    post_update_deps('debug')
    post_update_deps('release')

    print '- running gyp...'
    run_all_gyps()
    print ''

    print 'Success!'
    return 0


def run_gyp(format):
  """Runs gyp on the main project with the given format.

  Args:
    format: gyp -f value.
  """
  shell_call(' '.join([
      'third_party/gyp/gyp',
      '-f %s' % (format),
      # Set the VS version.
      # TODO(benvanik): allow user to set?
      '-G msvs_version=2010',
      # Removes the out/ from ninja builds.
      '-G output_dir=.',
      '--depth=.',
      '--generator-output=build/xenia/',
      'xenia.gyp',
      ]))


def run_all_gyps():
  """Runs all gyp configurations.
  """
  run_gyp('ninja')
  run_gyp('xcode')
  run_gyp('msvs')


class GypCommand(Command):
  """'gyp' command."""

  def __init__(self, *args, **kwargs):
    super(GypCommand, self).__init__(
        name='gyp',
        help_short='Runs gyp to update all projects.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print 'Running gyp...'
    print ''

    # Update GYP.
    run_all_gyps()

    print 'Success!'
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
    config = 'debug' if debug else 'release'

    # If there's no LLVM we may have been cleaned - run setup again.
    if not os.path.exists('build/llvm/'):
      print 'Missing LLVM, running setup...'
      shell_call('python xenia-build.py setup')
      print ''

    print 'Building %s...' % (config)
    print ''

    print '- running gyp for ninja...'
    run_gyp('ninja')
    print ''

    print '- building xenia in %s...' % (config)
    shell_call('third_party/ninja/ninja -C build/xenia/%s' % (config))
    print ''

    print 'Success!'
    return 0


class CleanCommand(Command):
  """'clean' command."""

  def __init__(self, *args, **kwargs):
    super(CleanCommand, self).__init__(
        name='clean',
        help_short='Removes intermediate files and build output.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print 'Cleaning build artifacts...'
    print ''

    print '- removing build/xenia/...'
    if os.path.isdir('build/xenia/'):
      shutil.rmtree('build/xenia/')
    print ''

    print 'Success!'
    return 0


class NukeCommand(Command):
  """'nuke' command."""

  def __init__(self, *args, **kwargs):
    super(NukeCommand, self).__init__(
        name='nuke',
        help_short='Removes all build/ output.',
        *args, **kwargs)

  def execute(self, args, cwd):
    print 'Cleaning build artifacts...'
    print ''

    print '- removing build/...'
    if os.path.isdir('build/'):
      shutil.rmtree('build/')
    print ''

    print 'Success!'
    return 0


if __name__ == '__main__':
  main()
