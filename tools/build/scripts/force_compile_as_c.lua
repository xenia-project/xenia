if premake.override then
  local forced_c_files = {}

  -- Forces all of the given .c and .cc files to be compiled as if they were C.
  function force_compile_as_c(files)
    for _, val in ipairs(files) do
      for _, fname in ipairs(os.matchfiles(val)) do
        table.insert(forced_c_files, path.getabsolute(fname))
      end
    end
  end

  -- for gmake
  premake.override(path, "iscfile", function(base, fname)
    if table.contains(forced_c_files, fname) then
      return true
    else
      return base(fname)
    end
  end)
  -- for msvc
  premake.override(premake.vstudio.vc2010, "additionalCompileOptions", function(base, cfg, condition)
    if cfg.abspath and table.contains(forced_c_files, cfg.abspath) then
      if condition == nil or condition == '' then
        _p(3,'<CompileAs>CompileAsC</CompileAs>')
      else
        _p(3,'<CompileAs Condition="\'$(Configuration)|$(Platform)\'==\'%s\'">CompileAsC</CompileAs>', condition)
      end
    end
    return base(cfg, condition)
  end)
end
