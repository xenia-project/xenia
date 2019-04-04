project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-net-proxy")
  uuid("02A982D7-7E59-45DF-8402-46C84127652F")
  kind("WindowedApp")
  language("C++")
  links({
    project_root.."/third_party/winpcap/Lib/x64/Packet",
	project_root.."/third_party/winpcap/Lib/x64/wpcap",
	"gflags",
    "xenia-base",
    "xxhash",
  })
  flags({
    "WinMain",  -- Use WinMain instead of main.
  })
  defines({
    "HAVE_REMOTE=1"
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
	project_root.."/third_party/winpcap/Include",
    project_root.."/build_tools/third_party/gflags/src",
  })
  files({
    "net_proxy_main.cc",
	"net_proxy.h",
	"../base/main_"..platform_suffix..".cc",
  })
	
  filter("platforms:Windows")
  debugdir(project_root)
  debugargs({
    "--flagfile=scratch/flags.txt",
    "2>&1",
    "1>scratch/stdout-net-proxy.txt",
  })