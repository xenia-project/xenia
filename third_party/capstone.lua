group("third_party")
project("capstone")
  uuid("b3a89f7e-bb02-4945-ae75-219caed6afa2")
  kind("StaticLib")
  language("C")
  defines({
    "CAPSTONE_X86_ATT_DISABLE",
    "CAPSTONE_DIET_NO",
    "CAPSTONE_X86_REDUCE_NO",
    "CAPSTONE_HAS_X86",
    "CAPSTONE_USE_SYS_DYN_MEM",
    "_LIB",
  })
--  filter({"configurations:Release", "platforms:Windows"})
--    buildoptions({
--      "/O1",
--    })
--  filter {}

  includedirs({
    "capstone",
    "capstone/include",
  })
  files({
    "capstone/cs.c",
    "capstone/cs_priv.h",
    "capstone/LEB128.h",
    "capstone/MathExtras.h",
    "capstone/MCDisassembler.h",
    "capstone/MCFixedLenDisassembler.h",
    "capstone/MCInst.c",
    "capstone/MCInst.h",
    "capstone/MCInstrDesc.c",
    "capstone/MCInstrDesc.h",
    "capstone/MCRegisterInfo.c",
    "capstone/MCRegisterInfo.h",
    "capstone/SStream.c",
    "capstone/SStream.h",
    "capstone/utils.c",
    "capstone/utils.h",
    "capstone/Mapping.c",
    "capstone/Mapping.h",

    "capstone/arch/X86/*.c",
    "capstone/arch/X86/*.h",
    "capstone/arch/X86/*.inc",
  })
  force_compile_as_c({
    "capstone/**.c",
    "capstone/arch/X86/**.c",
  })
