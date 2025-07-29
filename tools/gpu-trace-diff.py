#!/usr/bin/env python3

# Copyright 2015 Ben Vanik. All Rights Reserved.

"""GPU trace runner and comparison tool.
"""

__author__ = 'ben.vanik@gmail.com (Ben Vanik)'


import argparse
import hashlib
import math
import operator
import os
import shutil
import subprocess
import sys

self_path = os.path.dirname(os.path.abspath(__file__))


try:
  from PIL import Image, ImageChops
except:
  print 'ERROR: could not find ImageChops - install with:'
  print '  python -m pip install Pillow'
  sys.exit(1)


def main():
  # Add self to the root search path.
  sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))

  # Check python version.
  if not sys.version_info[:2] == (2, 7):
    # TODO(benvanik): allow things to work with 3, but warn on clang-format.
    print('ERROR: Python 2.7 must be installed and on PATH')
    sys.exit(1)
    return

  # Setup main argument parser and common arguments.
  parser = argparse.ArgumentParser(prog='gpu-trace-diff.py',
                                   description='Run and diff GPU traces.')
  parser.add_argument(
      '-x', '--executable',
      default='build/bin/Windows/Debug/xenia-gpu-vulkan-trace-dump.exe')
  parser.add_argument('-t', '--trace_file', action='append')
  parser.add_argument('-p', '--trace_path')
  parser.add_argument('-o', '--output_path', default='')
  parser.add_argument('-r', '--reference_path', default='')
  parser.add_argument('-u', '--update_reference_files', action='store_true')
  parser.add_argument('-n', '--generate_missing_reference_files',
                      action='store_true')
  args = vars(parser.parse_args(sys.argv[1:]))

  exe_path = args['executable']
  if not os.path.exists(exe_path):
    print 'ERROR: executable %s not found, ensure it is built' % (exe_path)
    sys.exit(1)
    return

  trace_files = args['trace_file'] or []
  if args['trace_path']:
    for child_path in os.listdir(args['trace_path']):
      if (os.path.splitext(child_path)[1] == '.xenia_gpu_trace'):
        trace_files.append(os.path.join(args['trace_path'], child_path))

  # If the user passed no args, die nicely.
  if not trace_files:
    parser.print_help()
    sys.exit(1)
    return

  output_path = args['output_path'].replace('/', os.pathsep)
  if not os.path.exists(output_path):
    os.makedirs(output_path)

  html_path = os.path.join(output_path, 'results.html')
  if os.path.exists(html_path):
    os.remove(html_path)

  reference_path = args['reference_path'].replace('/', os.pathsep)
  if not os.path.exists(reference_path):
    print('Reference path %s not found; forcing to update mode')
    os.makedirs(reference_path)
    args['update_reference_files'] = True

  html_file = None
  if not args['update_reference_files']:
    html_file = open(html_path, 'w')
    html_file.write("""
<html><head>
<title>GPU Trace Comparison Results</title>
</head><body>
<table>
<tr>
<td>Output</td>
<td>Diff</td>
<td>Reference</td>
</tr>
""")
    html_rel_path = os.path.relpath(os.getcwd(), output_path)
    html_file.write('<base href="%s">' % html_rel_path)

  diff_count = 0
  for trace_file in trace_files:
    trace_file = trace_file.replace('/', os.pathsep)
    base_path = os.path.dirname(trace_file)
    file_name = os.path.basename(trace_file)
    reference_file_path = os.path.join(reference_path, file_name + '.png')
    output_file_path = os.path.join(output_path, file_name + '.png')
    diff_file_path = os.path.join(output_path, file_name + '.diff.png')

    if (args['generate_missing_reference_files'] and
        os.path.exists(reference_file_path)):
      # Only process tracess that are missing reference files.
      continue

    print '--------------------------------------------------------------------'
    print '    Trace: %s' % (trace_file)
    print 'Reference: %s' % (reference_file_path)

    # Cleanup old files.
    if os.path.exists(output_file_path):
      os.remove(output_file_path)
    if os.path.exists(diff_file_path):
      os.remove(diff_file_path)

    # Run the trace dump too to produce a new png.
    run_args = [
        exe_path.replace('/', os.pathsep),
        '--target_trace_file=%s' % (trace_file.replace(os.pathsep, '/')),
        '--trace_dump_path=%s' % (output_path.replace(os.pathsep, '/')),
        ]
    tries_remaining = 3
    while tries_remaining:
      try:
        startupinfo = None
        if os.name == 'nt':
          startupinfo = subprocess.STARTUPINFO()
          startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
          startupinfo.wShowWindow = 4  # SW_SHOWNOACTIVATE
        proc = subprocess.Popen(
            run_args,
            shell=True,
            startupinfo=startupinfo,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        out, err = proc.communicate()
        # print out
        # print err
      except Exception as e:
        print 'ERROR: failed to run trace dump tool'
        print run_args
        print e
        sys.exit(1)
        return

      # Ensure the file was generated
      if not os.path.exists(output_file_path):
        print 'New image not generated; died?'
        tries_remaining -= 1
        continue
      else:
        break

    if not tries_remaining and not os.path.exists(output_file_path):
      print 'Retries exceeded; aborting'
      sys.exit(1)
      return

    # If updating references, move the output into place.
    if args['update_reference_files']:
      print 'Updating reference file...'
      if not os.path.exists(os.path.dirname(reference_file_path)):
        os.makedirs(os.path.dirname(reference_file_path))
      shutil.copy2(output_file_path, reference_file_path)
      continue

    # If we didn't have a reference file for this and are in gen mode, just copy
    # and ignore.
    if (not os.path.exists(reference_file_path) and
        args['generate_missing_reference_files']):
      print 'Adding new reference file...'
      if not os.path.exists(os.path.dirname(reference_file_path)):
        os.makedirs(os.path.dirname(reference_file_path))
      shutil.copy2(output_file_path, reference_file_path)
      continue

    # Compare files.
    print '      New: %s' % (output_file_path)
    reference_image = Image.open(reference_file_path)
    reference_rgb = reference_image.convert('RGB').tobytes('raw', 'RGB')
    reference_hash = hashlib.sha1(reference_rgb).hexdigest()
    new_image = Image.open(output_file_path)
    new_rgb = new_image.convert('RGB').tobytes('raw', 'RGB')
    new_hash = hashlib.sha1(new_rgb).hexdigest()
    is_diff = reference_hash != new_hash
    if is_diff:
      print 'DIFFERENCE DETECTED! %s != %s' % (reference_hash, new_hash)
      diff_count += 1
      diff_image = ImageChops.difference(reference_image, new_image)
      diff_image.save(diff_file_path)
    else:
      print 'Matches exactly!'

    if html_file:
      html_file.write('<tr>')
      html_file.write('<td colspan="3">%s</td>' % (trace_file))
      html_file.write('</tr>')
      html_file.write('<tr>')
      if is_diff:
        html_file.write(
            '<td><img class="image output-image" src="%s"></td>' % (
                output_file_path))
        html_file.write(
            '<td><img class="image diff-image" src="%s"></td>' % (
                diff_file_path))
        html_file.write(
            '<td><img class="image reference-image" src="%s"></td>' % (
                reference_file_path))
      else:
        html_file.write(
            '<td><img class="image reference-image" src="%s"></td>' % (
                reference_file_path))
        html_file.write('<td></td><td></td>')
      html_file.write('</tr>')

    print ''

  if html_file:
    html_file.write("""
</table>
<script>
function toggleSizes() {
  [].forEach.call(document.getElementsByClassName('image'), function(el) {
    if (el.style.width != '480px') {
      el.style.width = '480px';
    } else {
      el.style.width = '';
    }
  });
}
[].forEach.call(document.getElementsByClassName('image'), function(el) {
  el.onclick = toggleSizes;
});
toggleSizes();
</script>
</body></html>
""")
    html_file.close()

  if not diff_count:
    print 'All files match!'
    sys.exit(0)
  else:
    print '%s files do not match!' % (diff_count)
    sys.exit(1)

if __name__ == '__main__':
  main()
