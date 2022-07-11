include("build_paths.lua")
include("util.lua")

newoption({
  trigger = "test-suite-mode",
  description = "Whether to merge all tests in a test_suite into a single project",
  value = "MODE",
  allowed = {
    { "individual", "One binary per test." },
    { "combined", "One binary per test suite (default)." },
  },
})

local function combined_test_suite(test_suite_name, project_root, base_path, config)
  group("tests")
  project(test_suite_name)
    kind("ConsoleApp")
    language("C++")
    includedirs(merge_arrays(config["includedirs"], {
      project_root.."/"..build_tools,
      project_root.."/"..build_tools_src,
      project_root.."/"..build_tools.."/third_party/catch/include",
    }))
    libdirs(merge_arrays(config["libdirs"], {
      project_root.."/"..build_bin,
    }))
    links(config["links"])
    if config.filtered_links ~= nil then
      for _, filtered_links in ipairs(config.filtered_links) do
        filter(filtered_links.filter)
        links(filtered_links.links)
      end
      filter({})
    end
    defines({
      "XE_TEST_SUITE_NAME=\""..test_suite_name.."\"",
    })
    files({
      project_root.."/"..build_tools_src.."/test_suite_main.cc",
      project_root.."/src/xenia/base/console_app_main_"..platform_suffix..".cc",
      base_path.."/**_test.cc",
    })
    filter("toolset:msc")
      -- Edit and Continue in MSVC can cause the __LINE__ macro to produce
      -- invalid values, which breaks the usability of Catch2 output on
      -- failed tests.
      editAndContinue("Off")
end

local function split_test_suite(test_suite_name, project_root, base_path, config)
  local test_paths = os.matchfiles(base_path.."/**_test.cc")
  for _, file_path in pairs(test_paths) do
    local test_name = file_path:match("(.*).cc")
    group("tests/"..test_suite_name)
    project(test_suite_name.."-"..test_name)
      kind("ConsoleApp")
      language("C++")
      includedirs(merge_arrays(config["includedirs"], {
        project_root.."/"..build_tools,
        project_root.."/"..build_tools_src,
        project_root.."/"..build_tools.."/third_party/catch/include",
      }))
      libdirs(merge_arrays(config["libdirs"], {
        project_root.."/"..build_bin,
      }))
      links(config["links"])
      if config.filtered_links ~= nil then
        for _, filtered_links in ipairs(config.filtered_links) do
          filter(filtered_links.filter)
          links(filtered_links.links)
        end
        filter({})
      end
      files({
        project_root.."/"..build_tools_src.."/test_suite_main.cc",
        file_path,
      })
      filter("toolset:msc")
        -- Edit and Continue in MSVC can cause the __LINE__ macro to produce
        -- invalid values, which breaks the usability of Catch2 output on
        -- failed tests.
        editAndContinue("Off")
  end
end

-- Defines a test suite binary.
-- Can either be a single binary with all tests or one binary per test based on
-- the --test-suite-mode= option.
function test_suite(
    test_suite_name,        -- Project or group name for the entire suite.
    project_root,           -- Project root path (with build_tools/ under it).
    base_path,              -- Base source path to search for _test.cc files.
    config)                 -- Include/lib directories and links for binaries.
  if _OPTIONS["test-suite-mode"] == "individual" then
    split_test_suite(test_suite_name, project_root, base_path, config)
  else
    combined_test_suite(test_suite_name, project_root, base_path, config)
  end
end
