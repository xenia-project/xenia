if premake.override then
  local forced_cc_files = {}

  -- Forces all of the given .c files to be compiled as if they were C++.
  function force_compile_as_cc(files)
    for _, val in ipairs(files) do
      for _, fname in ipairs(os.matchfiles(val)) do
        table.insert(forced_cc_files, path.getabsolute(fname))
      end
    end
  end

  -- for gmake
  premake.override(path, "iscfile", function(base, fname)
    if table.contains(forced_cc_files, fname) then
      return false
    else
      return base(fname)
    end
  end)
  -- for msvc
  premake.override(premake.vstudio.vc2010, "additionalCompileOptions", function(base, cfg, condition)
    if cfg.abspath and table.contains(forced_cc_files, cfg.abspath) then
      if condition == nil or condition == '' then
        _p(3,'<CompileAs>CompileAsCpp</CompileAs>')
      else
        _p(3,'<CompileAs Condition="\'$(Configuration)|$(Platform)\'==\'%s\'">CompileAsCpp</CompileAs>', condition)
      end
    end
    return base(cfg, condition)
  end)
end
