-- Helper methods to use the system pkg-config utility

pkg_config = {}

function pkg_config.cflags(lib)
  if not os.istarget("linux") then
    return
  end
  buildoptions({
    ({os.outputof("pkg-config --cflags "..lib)})[1],
  })
end

function pkg_config.lflags(lib)
  if not os.istarget("linux") then
    return
  end
  linkoptions({
    ({os.outputof("pkg-config --libs-only-L "    ..lib)})[1],
    ({os.outputof("pkg-config --libs-only-other "..lib)})[1],
  })
  -- We can't just drop the stdout of the `--libs` command in
  -- linkoptions because library order matters
  local output = ({os.outputof("pkg-config --libs-only-l "..lib)})[1]
  for k, flag in next, string.explode(output, " ") do
    -- remove "-l"
    links(string.sub(flag, 3))
  end
end

function pkg_config.all(lib)
    pkg_config.cflags(lib)
    pkg_config.lflags(lib)
end
