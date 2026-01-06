-- Helper methods to use the system pkg-config utility

pkg_config = {}

local function pkg_config_call(lib, what)
  local result, code = os.outputof("pkg-config --"..what.." "..lib)
  if result then
    return result
  else
    error("Failed to run 'pkg-config' for library '"..lib.."'. Are the development files installed?")
  end
end

function pkg_config.cflags(lib)
  if not (os.istarget("linux") or os.istarget("macosx")) then
    return
  end
  buildoptions({
    pkg_config_call(lib, "cflags"),
  })
end

function pkg_config.lflags(lib)
    if not (os.istarget("linux") or os.istarget("macosx")) then
    return
  end
  linkoptions({
    pkg_config_call(lib, "libs-only-L"),
    pkg_config_call(lib, "libs-only-other"),
  })
  -- We can't just drop the stdout of the `--libs` command in
  -- linkoptions because library order matters
  local output = pkg_config_call(lib, "libs-only-l")
  for k, flag in next, string.explode(output, " ") do
    -- remove "-l"
    if flag ~= "" then
        links(string.sub(flag, 3))
    end
  end
end

function pkg_config.all(lib)
    pkg_config.cflags(lib)
    pkg_config.lflags(lib)
end
