project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-skylander")
  uuid("ddc114da-a279-4868-8b20-53108599bd78")
  kind("StaticLib")
  language("C++")
  files({
      "skylander_portal.h",
      "skylander_portal.cc",
      "skylander_emulated.h",
      "skylander_emulated.cc"
  })

  filter({"platforms:Windows"})
    links({
      "libusb",
    })
    files({
      "skylander_hardware.h",
      "skylander_hardware.cc"
    })
