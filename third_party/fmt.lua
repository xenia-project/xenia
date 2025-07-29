group("third_party")
project("fmt")
  uuid("b9ff4b2c-b438-42a9-971e-e0c19a711a13")
  kind("StaticLib")
  language("C++")

  includedirs({
    "fmt/include",
  })
  files({
    "fmt/src/format.cc",
    "fmt/src/os.cc"
  })
