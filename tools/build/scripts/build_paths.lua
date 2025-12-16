build_root = "build"
build_bin = build_root .. "/bin/%{cfg.platform}/%{cfg.buildcfg}"
build_gen = build_root .. "/gen/%{cfg.platform}/%{cfg.buildcfg}"
-- For VS projects, premake's token expansion can double up the path.
-- Use simpler path for Windows, full path for other platforms.
if os.istarget("windows") then
  build_obj = build_root .. "/obj/%{prj.name}"
else
  build_obj = build_root .. "/obj/%{cfg.platform}/%{cfg.buildcfg}"
end

build_tools = "tools/build"
build_scripts = build_tools .. "/scripts"
build_tools_src = build_tools .. "/src"

if os.istarget("android") then
  platform_suffix = "android"
elseif os.istarget("windows") then
  platform_suffix = "win"
else
  platform_suffix = "posix"
end
