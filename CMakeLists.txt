cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(Scream
    VERSION 1.0.11
    DESCRIPTION ""
    HOMEPAGE_URL "https://github.com/Cure-Audio/Scream"
    LANGUAGES C)
set(COMPANY_NAME "Cure Audio")

set(PRODUCT_NAME ${PROJECT_NAME}) # used by info.plist
set(PRODUCT_COPYRIGHT ${COMPANY_NAME})
set(AUV2_BUNDLE_ID "com.cureaudio.scream.auv2")
set(CLAP_BUNDLE_ID "com.cureaudio.scream.clap")
set(VST3_BUNDLE_ID "com.cureaudio.scream.vst3")
set(APP_BUNDLE_ID "com.cureaudio.scream.app")
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_TAGS "<string>Filter</string>") # Predefined tags are: Bass, Delay, Distortion, Drums, Dynamics, Dynamics Processor, Effects, Equalizer, Filter, Format Converter, Guitar, Imaging, MIDI, Mixer, Offline Effect, Output, Panner, Pitch, Reverb, Sampler, Synthesizer, Time Effect, Vocal. But you can use others.
set(MACOSX_BUNDLE_TYPE "aufx") # "aufx" for Effect, "augn" for Generator, "aumu" for Instrument, "aufm" for Music Effect.
set(MACOSX_BUNDLE_SUBTYPE "Scrm") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
set(MACOSX_BUNDLE_MANUFACTURER "Cure") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
# Convert version string (1.0.0) > integer (65536)
math(EXPR MACOSX_BUNDLE_VERSION_INT "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT DECIMAL) # cool trick bro


set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_C_STANDARD 11)
else()
    set(CMAKE_C_STANDARD 99)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 11)
endif()

set(PLUGIN_OPTIONS "")

# https://clang.llvm.org/docs/ClangCommandLineReference.html
if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    list(APPEND PLUGIN_OPTIONS
        -Werror=switch
        -Werror=absolute-value
        -Werror=return-type
        -Werror=shadow
        -Werror=incompatible-pointer-types
        -Werror=parentheses
        -Werror=excess-initializers
        -Werror=array-bounds
        # -Wunused-function
        # -Wunused-variable
        
        -Wno-deprecated-declarations
        -Wno-deprecated
        -Wno-multichar
        -Wno-nullability-completeness
        -Wno-writable-strings
        -Wno-c2x-extensions
        -Wno-c++14-extensions
        -Wno-c++17-extensions
        -Wno-c++20-extensions
        -Wno-microsoft-enum-forward-reference
        )
endif()
# list(APPEND CMAKE_C_FLAGS -fsanitize=address)
# list(APPEND CMAKE_CXX_FLAGS -fsanitize=address)

if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND PLUGIN_OPTIONS /GR- /EHasc- "/FI${PROJECT_SOURCE_DIR}/src/common.h")
    list(APPEND PLUGIN_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/Zi>")
    add_link_options("$<$<CONFIG:RELEASE>:/DEBUG>")
    if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        list(APPEND PLUGIN_OPTIONS /Od)
    endif()
else()
    list(APPEND PLUGIN_OPTIONS -fno-rtti -fno-exceptions)
    if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        list(APPEND PLUGIN_OPTIONS -fno-inline-functions)
    endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
    # Currently getting release mode bugs on MacOS & Windows...
    # list(APPEND PLUGIN_OPTIONS -flto)
endif()

set(SRC_DIR ${PROJECT_SOURCE_DIR})
set(BIN_DIR ${CMAKE_BINARY_DIR})
if (WIN32)
    # Windows paths are complicated
    string(REPLACE "/" "\\\\" SRC_DIR ${SRC_DIR})
    string(REPLACE "/" "\\\\" BIN_DIR ${BIN_DIR})
endif()

set(PLUGIN_DEFINITIONS
    CPLUG_COMPANY_NAME="${COMPANY_NAME}"
    CPLUG_COMPANY_EMAIL="contact@cure.audio"
    CPLUG_PLUGIN_NAME="${PROJECT_NAME}"
    CPLUG_PLUGIN_URI="${PROJECT_HOMEPAGE_URL}"
    CPLUG_PLUGIN_VERSION="${PROJECT_VERSION}"
    _HAS_EXCEPTIONS=0
    STBI_NO_STDIO
    STBI_NO_LINEAR
    NVG_NO_STB
    SRC_DIR="${SRC_DIR}"
    BIN_DIR="${BIN_DIR}"
    PW_PREFIX=${PROJECT_NAME}
    )

set(PLUGIN_SOURCES
    src/plugin.c
    src/state.c
    
    src/gui.c

    src/gui_lfo.c
    src/im_points.c
    src/resource_manager.c
    src/icons.c
    src/logger.c
    src/updates.c

    src/libs/freetype_scu.c
    src/libs/nanosvgrast3.c
    src/libs/stb_rect_pack.c
    src/libs/stb_image.c
    src/libs/stb_image_resize2.c
    src/libs/texteditor.c
    src/libs/tooltip.c
    src/libs/picohttpparser.c
    src/libs/yyjson.c
    src/libs/https.c
    
    # modules/wip_libs/src/nanovg2.c
    modules/xvg/xvg.c
    modules/wip_libs/src/imgui.c
    modules/wip_libs/src/linked_arena.c
    modules/wip_libs/src/xrequest.c
    )

set(PLUGIN_INCLUDE
    modules/xhl/include
    modules/CPLUG/src/
    modules/sokol_gfx_multiinstance
    modules/wip_libs/src/
    modules/xvg/
    modules/freetype/include
    modules/freetype/src/base

    src/libs
    src/shaders/
    )

set(PLUGIN_LIBRARIES "")

if (WIN32)
    list(APPEND PLUGIN_DEFINITIONS UNICODE _UNICODE _USE_MATH_DEFINES PW_DX11)

    if (CMAKE_BUILD_TYPE MATCHES Release)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            list(APPEND PLUGIN_OPTIONS /arch:AVX)
        else()
            list(APPEND PLUGIN_OPTIONS -march=corei7-avx)
        endif()
    else()
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            list(APPEND PLUGIN_OPTIONS /arch:AVX2)
        else()
            list(APPEND PLUGIN_OPTIONS -march=haswell)
        endif()
    endif()
    list(APPEND PLUGIN_SOURCES
        modules/sokol_gfx_multiinstance/sokol_gfx_d3d11.c
        modules/CPLUG/src/cplug_extensions/window_win.c
        src/platform_win.c)
    list(APPEND PLUGIN_LIBRARIES dxguid)
elseif(APPLE)
    enable_language(OBJC)
    # if (CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    list(APPEND PLUGIN_OPTIONS -g)
    # endif()

    list(APPEND PLUGIN_SOURCES
        modules/sokol_gfx_multiinstance/sokol_gfx_metal.m
        modules/CPLUG/src/cplug_extensions/window_osx.m
        src/platform_osx.m)
    list(APPEND PLUGIN_LIBRARIES
        "-framework Quartz"
        "-framework Cocoa"
        "-framework AudioToolbox"
        "-framework Metal"
        "-framework MetalKit")
    list(APPEND PLUGIN_DEFINITIONS PW_METAL)
endif()


# ██████╗ ██╗     ██╗   ██╗ ██████╗ ██╗███╗   ██╗
# ██╔══██╗██║     ██║   ██║██╔════╝ ██║████╗  ██║
# ██████╔╝██║     ██║   ██║██║  ███╗██║██╔██╗ ██║
# ██╔═══╝ ██║     ██║   ██║██║   ██║██║██║╚██╗██║
# ██║     ███████╗╚██████╔╝╚██████╔╝██║██║ ╚████║
# ╚═╝     ╚══════╝ ╚═════╝  ╚═════╝ ╚═╝╚═╝  ╚═══╝

# .plists are generated using a template
# The params @bundle_identifier@ and @bundle_type@ are used by the .plist
function(configure_info_plist target_name bundle_identifier bundle_type bundle_extension)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/CPLUG/example/Info.plist.in
        ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.${bundle_extension}/Contents/Info.plist
        @ONLY
    )

    set_target_properties(${target_name} PROPERTIES
        BUNDLE True
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME ${PROJECT_NAME}
        BUNDLE_EXTENSION ${bundle_extension}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.${bundle_extension}/Contents/Info.plist
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.${bundle_extension}
    )
endfunction()

# All Plugin formats in one
if (APPLE)
    add_library(${PROJECT_NAME}_auv2 MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_auv2.c)
    add_library(${PROJECT_NAME}_clap MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_clap.c)
    add_library(${PROJECT_NAME}_vst3 MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_vst3.c)
    configure_info_plist(${PROJECT_NAME}_auv2 ${AUV2_BUNDLE_ID} "BNDL" "component")
    configure_info_plist(${PROJECT_NAME}_clap ${CLAP_BUNDLE_ID} "BNDL" "clap")
    configure_info_plist(${PROJECT_NAME}_vst3 ${VST3_BUNDLE_ID} "BNDL" "vst3")

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component/Contents/PkgInfo" "BNDL????")
    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo" "BNDL????")
    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3/Contents/PkgInfo" "BNDL????")

    target_compile_definitions(${PROJECT_NAME}_auv2 PRIVATE
         ${PLUGIN_DEFINITIONS}
        CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
        CPLUG_AUV2_BUNDLE_ID="${AUV2_BUNDLE_ID}"
        CPLUG_BUILD_AUV2
    )
    target_compile_definitions(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_CLAP)
    target_compile_definitions(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_VST3)

    target_link_libraries(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_LIBRARIES})
    target_link_libraries(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_LIBRARIES})
    target_link_libraries(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_LIBRARIES})
    target_include_directories(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_INCLUDE})
    target_include_directories(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_INCLUDE})
    target_include_directories(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_INCLUDE})
    target_compile_options(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_OPTIONS})
    target_compile_options(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_OPTIONS})
    target_compile_options(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_OPTIONS})
    target_precompile_headers(${PROJECT_NAME}_auv2 PRIVATE src/common.h)
    target_precompile_headers(${PROJECT_NAME}_clap PRIVATE src/common.h)
    target_precompile_headers(${PROJECT_NAME}_vst3 PRIVATE src/common.h)

    add_custom_command(TARGET ${PROJECT_NAME}_auv2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component to ~/Library/Audio/Plug-Ins/Components/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component" "~/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component"
    )
    add_custom_command(TARGET ${PROJECT_NAME}_clap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap to ~/Library/Audio/Plug-Ins/CLAP/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "~/Library/Audio/Plug-Ins/CLAP/${PROJECT_NAME}.clap"
    )
    add_custom_command(TARGET ${PROJECT_NAME}_vst3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3 to ~/Library/Audio/Plug-Ins/VST3/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3" "~/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3"
    )
elseif(WIN32)
    add_library(${PROJECT_NAME}_plugin MODULE
        ${PLUGIN_SOURCES}
        modules/CPLUG/src/cplug_clap.c
        modules/CPLUG/src/cplug_vst3.c)
    set_target_properties(${PROJECT_NAME}_plugin PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}_plugin
        PDB_NAME ${PROJECT_NAME}_plugin
        )

    set(VST3_BUILD_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3)
    add_custom_command(TARGET ${PROJECT_NAME}_plugin POST_BUILD
        # https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Format.html
        COMMAND ${CMAKE_COMMAND} -E echo "Building VST3"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${VST3_BUILD_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}/Contents"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}/Contents/x86_64-win"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll" "${VST3_BUILD_PATH}/Contents/x86_64-win/${PROJECT_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/PlugIn.ico"                      "${VST3_BUILD_PATH}/PlugIn.ico"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/desktop.ini.in"                  "${VST3_BUILD_PATH}/desktop.ini"
        COMMAND attrib +s +h ${VST3_BUILD_PATH}/desktop.ini
        COMMAND attrib +s +h ${VST3_BUILD_PATH}/PlugIn.ico
        COMMAND attrib +s ${VST3_BUILD_PATH}

        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${VST3_BUILD_PATH} to %LOCALAPPDATA%\\Programs\\Common\\VST3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory  "${VST3_BUILD_PATH}" "%LOCALAPPDATA%\\Programs\\Common\\VST3\\${PROJECT_NAME}.vst3"

        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll to %LOCALAPPDATA%\\Programs\\Common\\CLAP\\${PROJECT_NAME}.clap"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll" "%LOCALAPPDATA%\\Programs\\Common\\CLAP\\${PROJECT_NAME}.clap"
        )
    target_link_libraries(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_LIBRARIES})
    target_include_directories(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_INCLUDE})
    target_compile_definitions(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_VST3 CPLUG_BUILD_CLAP CPLUG_BUILD_AUV2)
    target_compile_options(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_OPTIONS})
    target_precompile_headers(${PROJECT_NAME}_plugin PRIVATE src/common.h)
endif()


# ███████╗████████╗ █████╗ ███╗   ██╗██████╗  █████╗ ██╗      ██████╗ ███╗   ██╗███████╗
# ██╔════╝╚══██╔══╝██╔══██╗████╗  ██║██╔══██╗██╔══██╗██║     ██╔═══██╗████╗  ██║██╔════╝
# ███████╗   ██║   ███████║██╔██╗ ██║██║  ██║███████║██║     ██║   ██║██╔██╗ ██║█████╗  
# ╚════██║   ██║   ██╔══██║██║╚██╗██║██║  ██║██╔══██║██║     ██║   ██║██║╚██╗██║██╔══╝  
# ███████║   ██║   ██║  ██║██║ ╚████║██████╔╝██║  ██║███████╗╚██████╔╝██║ ╚████║███████╗
# ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝

if (WIN32)
    add_executable(${PROJECT_NAME}_standalone WIN32 ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_win.c)
    target_link_libraries(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_LIBRARIES})
elseif(APPLE)
    add_executable(${PROJECT_NAME}_standalone MACOSX_BUNDLE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_osx.m)
    target_link_libraries(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_LIBRARIES} "-framework CoreMIDI -framework CoreAudio -framework CoreServices")

    configure_info_plist(${PROJECT_NAME}_standalone ${APP_BUNDLE_ID} "APPL" "app")

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo" "APPL????")
endif()
target_include_directories(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_INCLUDE})
target_compile_definitions(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_STANDALONE)
target_compile_options(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_OPTIONS})
target_precompile_headers(${PROJECT_NAME}_standalone PRIVATE src/common.h)
# add_dependencies(${PROJECT_NAME}_standalone shader_header)

# ██╗  ██╗ ██████╗ ████████╗██████╗ ███████╗██╗      ██████╗  █████╗ ██████╗
# ██║  ██║██╔═══██╗╚══██╔══╝██╔══██╗██╔════╝██║     ██╔═══██╗██╔══██╗██╔══██╗
# ███████║██║   ██║   ██║   ██████╔╝█████╗  ██║     ██║   ██║███████║██║  ██║
# ██╔══██║██║   ██║   ██║   ██╔══██╗██╔══╝  ██║     ██║   ██║██╔══██║██║  ██║
# ██║  ██║╚██████╔╝   ██║   ██║  ██║███████╗███████╗╚██████╔╝██║  ██║██████╔╝
# ╚═╝  ╚═╝ ╚═════╝    ╚═╝   ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝

set(HOTRELOAD_LIB_NAME ${PROJECT_NAME}_hotreload_lib)
if (WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_library(${HOTRELOAD_LIB_NAME} MODULE ${PLUGIN_SOURCES})
    target_compile_definitions(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED CPLUG_BUILD_STANDALONE)
    target_include_directories(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_INCLUDE})
    target_compile_options(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_OPTIONS})

    # Windows paths are complicated
    string(REPLACE "/" "\\\\" SOURCE_DIR_WIN ${PROJECT_SOURCE_DIR})
    string(REPLACE "/" "\\\\" BINARY_DIR_WIN ${CMAKE_BINARY_DIR})

    add_executable(${PROJECT_NAME}_hotreload WIN32 modules/CPLUG/src/cplug_standalone_win.c)
    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE
        HOTRELOAD_WATCH_DIR="${SOURCE_DIR_WIN}\\\\src"
        HOTRELOAD_LIB_PATH="${BINARY_DIR_WIN}\\\\${CMAKE_BUILD_TYPE}\\\\${HOTRELOAD_LIB_NAME}.dll"
        HOTRELOAD_BUILD_COMMAND="cmake --build ${BINARY_DIR_WIN} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        CPLUG_SHARED
        )
    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED)
    target_include_directories(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_INCLUDE})
    target_compile_options(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_OPTIONS})
    # Forces plugin to be built before the host
    add_dependencies(${PROJECT_NAME}_hotreload ${HOTRELOAD_LIB_NAME})

    # add_dependencies(${HOTRELOAD_LIB_NAME} shader_header)
endif()

# Tool
# add_executable(svg_parser tools/svg_parser.c)
# target_compile_definitions(svg_parser PRIVATE ${PLUGIN_DEFINITIONS})
# target_include_directories(svg_parser PRIVATE ${PLUGIN_INCLUDE})
# target_compile_options(svg_parser PRIVATE ${PLUGIN_OPTIONS})
