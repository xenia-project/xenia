## api-scanner

api-scanner will dump out the API imports from a packaged 360 game

### Usage

Run from the command line

`api-scanner <package_path>`

or:

`api-scanner --target <package_path>`

Output is printed to the console window, so it self-managed.

The suggested usage is to append the output to a local file:

`api-scanner <package_path> >> title_log.txt`

### Issues

- Duplicate code for loading containers
- Several issues with gflags library - incorrectly prints usage from other files (due to linkage with libxenia)
