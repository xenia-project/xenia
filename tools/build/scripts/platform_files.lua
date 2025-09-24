include("build_paths.lua")
include("util.lua")

local function match_platform_files(base_path, base_match)
  files({
    base_path.."/"..base_match..".h",
    base_path.."/"..base_match..".c",
    base_path.."/"..base_match..".cc",
    base_path.."/"..base_match..".cpp",
    base_path.."/"..base_match..".inc",
  })
  removefiles({base_path.."/".."**_main.cc"})
  removefiles({base_path.."/".."**_test.cc"})
  removefiles({base_path.."/".."**_posix.h", base_path.."/".."**_posix.cc"})
  removefiles({base_path.."/".."**_linux.h", base_path.."/".."**_linux.cc"})
  removefiles({base_path.."/".."**_gnulinux.h",
               base_path.."/".."**_gnulinux.cc"})
  removefiles({base_path.."/".."**_x11.h", base_path.."/".."**_x11.cc"})
  removefiles({base_path.."/".."**_gtk.h", base_path.."/".."**_gtk.cc"})
  removefiles({base_path.."/".."**_android.h", base_path.."/".."**_android.cc"})
  removefiles({base_path.."/".."**_mac.h", base_path.."/".."**_mac.cc"})
  removefiles({base_path.."/".."**_win.h", base_path.."/".."**_win.cc"})
  filter("platforms:Windows-*")
    files({
      base_path.."/"..base_match.."_win.h",
      base_path.."/"..base_match.."_win.cc",
    })
  filter("platforms:Linux-* or Android-*")
    files({
      base_path.."/"..base_match.."_posix.h",
      base_path.."/"..base_match.."_posix.cc",
      base_path.."/"..base_match.."_linux.h",
      base_path.."/"..base_match.."_linux.cc",
    })
  filter("platforms:Linux-*")
    files({
      base_path.."/"..base_match.."_gnulinux.h",
      base_path.."/"..base_match.."_gnulinux.cc",
      base_path.."/"..base_match.."_x11.h",
      base_path.."/"..base_match.."_x11.cc",
      base_path.."/"..base_match.."_gtk.h",
      base_path.."/"..base_match.."_gtk.cc",
    })
  filter("platforms:Mac")
    -- First add all Mac-specific files
    files({
      base_path.."/"..base_match.."_mac.h",
      base_path.."/"..base_match.."_mac.cc",
    })
    -- Then add POSIX files as fallbacks
    files({
      base_path.."/"..base_match.."_posix.h",
      base_path.."/"..base_match.."_posix.cc",
    })
    -- For each Mac specific implementation file that exists, remove the corresponding POSIX fallback implementation
    local mac_files = os.matchfiles(base_path.."/"..base_match.."_mac.cc")
    for _, mac_file in ipairs(mac_files) do
      local posix_file = mac_file:gsub("_mac%.cc$", "_posix.cc")
      removefiles({posix_file})
    end
  filter("platforms:Android-*")
    files({
      base_path.."/"..base_match.."_android.h",
      base_path.."/"..base_match.."_android.cc",
    })
  filter({})
end

-- Adds all .h and .cc files in the current path that match the current platform
-- suffix (_win, etc).
function local_platform_files(base_path)
  match_platform_files(base_path or ".", "*")
end

-- Adds all .h and .cc files in the current path and all subpaths that match
-- the current platform suffix (_win, etc).
function recursive_platform_files(base_path)
  match_platform_files(base_path or ".", "**")
end
