-- Prints a table and all of its contents.
function print_r(t)
  local print_r_cache={}
  local function sub_print_r(t, indent)
    if (print_r_cache[tostring(t)]) then
      print(indent.."*"..tostring(t))
    else
      print_r_cache[tostring(t)]=true
      if (type(t)=="table") then
        for pos,val in pairs(t) do
          if (type(val)=="table") then
            print(indent.."["..pos.."] => "..tostring(t).." {")
            sub_print_r(val,indent..string.rep(" ",string.len(pos)+8))
            print(indent..string.rep(" ",string.len(pos)+6).."}")
          elseif (type(val)=="string") then
            print(indent.."["..pos..'] => "'..val..'"')
          else
            print(indent.."["..pos.."] => "..tostring(val))
          end
        end
      else
        print(indent..tostring(t))
      end
    end
  end
  if (type(t)=="table") then
    print(tostring(t).." {")
    sub_print_r(t,"  ")
    print("}")
  else
    sub_print_r(t,"  ")
  end
  print()
end

-- Merges two tables and returns the resulting table.
function merge_tables(t1, t2)
  local result = {}
  for k,v in pairs(t1 or {}) do result[k] = v end
  for k,v in pairs(t2 or {}) do result[k] = v end
  return result
end

-- Merges to arrays and returns the resulting array.
function merge_arrays(t1, t2)
  local result = {}
  for k,v in pairs(t1 or {}) do result[#result + 1] = v end
  for k,v in pairs(t2 or {}) do result[#result + 1] = v end
  return result
end
