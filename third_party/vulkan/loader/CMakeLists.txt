include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PROJECT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}
)

# Check for the existance of the secure_getenv or __secure_getenv commands
include(CheckFunctionExists)
CHECK_FUNCTION_EXISTS(secure_getenv HAVE_SECURE_GETENV)
CHECK_FUNCTION_EXISTS(__secure_getenv HAVE___SECURE_GETENV)
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/loader_cmake_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/loader_cmake_config.h)

# Fetch header version from vulkan_core.h
file(STRINGS "../include/vulkan/vulkan_core.h" lines REGEX "^#define VK_HEADER_VERSION [0-9]+")
list(LENGTH lines len)
if(${len} EQUAL 1)
    string(REGEX MATCHALL "[0-9]+" vk_header_version ${lines})
else()
    MESSAGE(FATAL_ERROR "Unable to fetch version from vulkan.h")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR -DWIN32_LEAN_AND_MEAN)
    set(DisplayServer Win32)
    if (NOT MSVC_VERSION LESS 1900)
        # Enable control flow guard
        message(STATUS "Building loader with control flow guard")
        add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/guard:cf>")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /guard:cf")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /guard:cf")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (BUILD_WSI_XCB_SUPPORT)
        add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
    endif()

    if (BUILD_WSI_XLIB_SUPPORT)
       add_definitions(-DVK_USE_PLATFORM_XLIB_KHR -DVK_USE_PLATFORM_XLIB_XRANDR_EXT)
    endif()

    if (BUILD_WSI_WAYLAND_SUPPORT)
       add_definitions(-DVK_USE_PLATFORM_WAYLAND_KHR)
    endif()

    if (BUILD_WSI_MIR_SUPPORT)
        add_definitions(-DVK_USE_PLATFORM_MIR_KHR)
        include_directories(${MIR_INCLUDE_DIR})
    endif()
else()
    message(FATAL_ERROR "Unsupported Platform!")
endif()

if (WIN32)
	FILE(TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vulkan-${MAJOR}.def DEF_FILE)
	FILE(READ ${DEF_FILE} DEF_TEXT)
	STRING(REGEX REPLACE "vulkan-${MAJOR}.dll" "${API_LOWERCASE}-${MAJOR}.dll" DEF_EDITED "${DEF_TEXT}" )
	FILE(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${API_LOWERCASE}-${MAJOR}.def" "${DEF_EDITED}")
endif()

# DEBUG enables runtime loader ICD verification
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

set(NORMAL_LOADER_SRCS
    extension_manual.c
    loader.c
    loader.h
    vk_loader_platform.h
    vk_loader_layer.h
    trampoline.c
    wsi.c
    wsi.h
    debug_utils.c
    debug_utils.h
    gpa_helper.h
    cJSON.c
    cJSON.h
    murmurhash.c
    murmurhash.h
)

set(OPT_LOADER_SRCS
    dev_ext_trampoline.c
    phys_dev_ext.c
)

# Check for assembler support
set(ASM_FAILURE_MSG "The build will fall back on building with C code\n")
set(ASM_FAILURE_MSG "${ASM_FAILURE_MSG}Note that this may be unsafe, as the C code requires tail-call optimizations to remove")
set(ASM_FAILURE_MSG "${ASM_FAILURE_MSG} the stack frame for certain calls. If the compiler does not do this, then unknown device")
set(ASM_FAILURE_MSG "${ASM_FAILURE_MSG} extensions will suffer from a corrupted stack.")
if (WIN32)
    enable_language(ASM_MASM)
    if (CMAKE_ASM_MASM_COMPILER_WORKS)
        if (NOT CMAKE_CL_64)
            set(CMAKE_ASM_MASM_FLAGS ${CMAKE_ASM_MASM_FLAGS} /safeseh)
        endif()
        set(OPT_LOADER_SRCS ${OPT_LOADER_SRCS} unknown_ext_chain_masm.asm)

        add_executable(asm_offset asm_offset.c)
        add_dependencies(asm_offset generate_helper_files loader_gen_files)
        add_custom_command(OUTPUT gen_defines.asm DEPENDS asm_offset COMMAND asm_offset MASM)
        add_custom_target(loader_asm_gen_files DEPENDS gen_defines.asm)
        set_target_properties(loader_asm_gen_files PROPERTIES FOLDER ${LVL_TARGET_FOLDER})
    else()
        message(WARNING "Could not find working MASM assebler\n${ASM_FAILURE_MSG}")
        set(OPT_LOADER_SRCS ${OPT_LOADER_SRCS} unknown_ext_chain.c)
        add_custom_target(loader_asm_gen_files)
    endif()
else()
    enable_language(ASM-ATT)
    set(CMAKE_ASM-ATT_COMPILE_FLAGS "${CMAKE_ASM-ATT_COMPILE_FLAGS} $ENV{ASFLAGS}")
    set(CMAKE_ASM-ATT_COMPILE_FLAGS "${CMAKE_ASM-ATT_COMPILE_FLAGS} -I\"${CMAKE_CURRENT_BINARY_DIR}\"")

    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/asm_test.asm ".intel_syntax noprefix\n.text\n.global sample\nsample:\nmov ecx, [eax + 16]\n")
    try_compile(ASSEMBLER_WORKS ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/asm_test.asm)
    file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/asm_test.asm)
    if (ASSEMBLER_WORKS)
        set(CMAKE_ASM-ATT_FLAGS "$ENV{ASFLAGS} -I\"${CMAKE_CURRENT_BINARY_DIR}\"")
        set(OPT_LOADER_SRCS ${OPT_LOADER_SRCS} unknown_ext_chain_gas.asm)
        add_executable(asm_offset asm_offset.c)
        add_dependencies(asm_offset generate_helper_files loader_gen_files)
        add_custom_command(OUTPUT gen_defines.asm DEPENDS asm_offset COMMAND asm_offset GAS)
        add_custom_target(loader_asm_gen_files DEPENDS gen_defines.asm)
    else()
        message(WARNING "Could not find working x86 GAS assembler\n${ASM_FAILURE_MSG}")
        set(OPT_LOADER_SRCS ${OPT_LOADER_SRCS} unknown_ext_chain.c)
        add_custom_target(loader_asm_gen_files)
    endif()
endif()

run_vk_xml_generate(loader_extension_generator.py vk_loader_extensions.h)
run_vk_xml_generate(loader_extension_generator.py vk_loader_extensions.c)
add_custom_target(loader_gen_files DEPENDS
        vk_loader_extensions.h
        vk_loader_extensions.c
    )
set_target_properties(loader_gen_files PROPERTIES FOLDER ${LVL_TARGET_FOLDER})

if (WIN32)
    # Use static MSVCRT libraries
    foreach(configuration in CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
                             CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${configuration} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${configuration} "${${configuration}}")
        endif()
    endforeach()

    if (ENABLE_WIN10_ONECORE)
    # Note  When linking your app or driver to OneCore.lib, be sure to remove any links to non-umbrella libs (such as kernel32.lib). 
        set(CMAKE_CXX_STANDARD_LIBRARIES " ") # space is intentional
        set(CMAKE_C_STANDARD_LIBRARIES ${CMAKE_CXX_STANDARD_LIBRARIES})
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS")
    # Build dev_ext_trampoline.c with -O2 to allow tail-call optimization.
    # Build other C files with normal options
    # setup two Cmake targets (loader-norm and loader-opt) for the different compilation flags
    separate_arguments(LOCAL_C_FLAGS_DBG WINDOWS_COMMAND ${CMAKE_C_FLAGS_DEBUG})
    set(CMAKE_C_FLAGS_DEBUG " ")
    separate_arguments(LOCAL_C_FLAGS_REL WINDOWS_COMMAND ${CMAKE_C_FLAGS_RELEASE})

    add_library(loader-norm-dy OBJECT ${NORMAL_LOADER_SRCS} dirent_on_windows.c)
    add_dependencies(loader-norm-dy generate_helper_files loader_gen_files)
    target_compile_definitions(loader-norm-dy PUBLIC LOADER_DYNAMIC_LIB)
    target_compile_options(loader-norm-dy PUBLIC "$<$<CONFIG:DEBUG>:${LOCAL_C_FLAGS_DBG}>")

    add_library(loader-opt-dy OBJECT ${OPT_LOADER_SRCS})
    add_dependencies(loader-opt-dy generate_helper_files loader_gen_files loader_asm_gen_files)
    target_compile_definitions(loader-opt-dy PUBLIC LOADER_DYNAMIC_LIB)
    target_compile_options(loader-opt-dy PUBLIC "$<$<CONFIG:DEBUG>:${LOCAL_C_FLAGS_REL}>")

    add_library(loader-norm-st OBJECT ${NORMAL_LOADER_SRCS} dirent_on_windows.c)
    add_dependencies(loader-norm-st generate_helper_files loader_gen_files)
    target_compile_options(loader-norm-st PUBLIC "$<$<CONFIG:DEBUG>:${LOCAL_C_FLAGS_DBG}>")

    add_library(loader-opt-st OBJECT ${OPT_LOADER_SRCS})
    add_dependencies(loader-opt-st generate_helper_files loader_gen_files loader_asm_gen_files)
    target_compile_options(loader-opt-st PUBLIC "$<$<CONFIG:DEBUG>:${LOCAL_C_FLAGS_REL}>")

    add_library(${API_LOWERCASE}-${MAJOR} SHARED $<TARGET_OBJECTS:loader-opt-dy> $<TARGET_OBJECTS:loader-norm-dy> ${CMAKE_CURRENT_BINARY_DIR}/${API_LOWERCASE}-${MAJOR}.def ${CMAKE_CURRENT_SOURCE_DIR}/loader.rc)
    add_library(VKstatic.${MAJOR} STATIC $<TARGET_OBJECTS:loader-opt-st> $<TARGET_OBJECTS:loader-norm-st>)
    # Suppress conflicting libs warning for debug builds.
    set_target_properties(${API_LOWERCASE}-${MAJOR} PROPERTIES LINK_FLAGS_DEBUG /ignore:4098)
    set_target_properties(VKstatic.${MAJOR} PROPERTIES OUTPUT_NAME VKstatic.${MAJOR})

    if (ENABLE_WIN10_ONECORE)
        target_link_libraries(${API_LOWERCASE}-${MAJOR} OneCoreUAP.lib LIBCMT.LIB LIBCMTD.LIB LIBVCRUNTIME.LIB LIBUCRT.LIB)
        set_target_properties(${API_LOWERCASE}-${MAJOR} PROPERTIES LINK_FLAGS   "/NODEFAULTLIB")
    else()
        target_link_libraries(${API_LOWERCASE}-${MAJOR} Cfgmgr32)
    endif()

    add_dependencies(${API_LOWERCASE}-${MAJOR} generate_helper_files loader_gen_files loader_asm_gen_files)

    if (CMAKE_GENERATOR MATCHES "^Visual Studio.*")
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIGURATION>/${API_LOWERCASE}-${MAJOR}.dll COPY_SRC_PATH)
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/../demos/$<CONFIGURATION>/ COPY_DST_PATH)
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/../tests/$<CONFIGURATION>/ COPY_DST_TEST_PATH)
    else()
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/${API_LOWERCASE}-${MAJOR}.dll COPY_SRC_PATH)
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/../demos/ COPY_DST_PATH)
        file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/../tests/ COPY_DST_TEST_PATH)
    endif()
    add_custom_command(TARGET ${API_LOWERCASE}-${MAJOR} POST_BUILD
      COMMAND xcopy /Y /I ${COPY_SRC_PATH} ${COPY_DST_PATH})
    add_custom_command(TARGET ${API_LOWERCASE}-${MAJOR} POST_BUILD
      COMMAND xcopy /Y /I ${COPY_SRC_PATH} ${COPY_DST_TEST_PATH})

else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith")

    # Clang (and not gcc) warns about redefining a typedef with the same types, so disable that warning.
    # Note that it will still throw an error if a typedef is redefined with a different type.
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-typedef-redefinition")
    endif()

    add_library(${API_LOWERCASE} SHARED ${NORMAL_LOADER_SRCS} ${OPT_LOADER_SRCS})
    add_dependencies(${API_LOWERCASE} generate_helper_files loader_gen_files loader_asm_gen_files)
    target_compile_definitions(${API_LOWERCASE} PUBLIC -DLOADER_DYNAMIC_LIB)
    set_target_properties(${API_LOWERCASE} PROPERTIES SOVERSION "1" VERSION "1.1.${vk_header_version}")
    target_link_libraries(${API_LOWERCASE} -ldl -lpthread -lm)

    if(INSTALL_LVL_FILES)
        install(TARGETS ${API_LOWERCASE} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()

    # Generate pkg-config file.
    include(FindPkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        set(VK_API_VERSION "1.1.${vk_header_version}")
        foreach(LIB ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES} ${PLATFORM_LIBS})
            set(PRIVATE_LIBS "${PRIVATE_LIBS} -l${LIB}")
        endforeach()
        configure_file("vulkan.pc.in" "vulkan.pc" @ONLY)
        if(INSTALL_LVL_FILES)
            install(FILES       "${CMAKE_CURRENT_BINARY_DIR}/vulkan.pc"
                    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
        endif()
    endif()
endif()
