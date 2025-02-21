cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(scream_template
    VERSION 0.0.1
    DESCRIPTION ""
    HOMEPAGE_URL "https://exacoustics.com"
    LANGUAGES C)
set(COMPANY_NAME exacoustics)

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
        -Werror=absolute-value
        -Werror=return-type
        -Werror=shadow
        -Werror=incompatible-pointer-types
        -Werror=parentheses
        -Werror=excess-initializers
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
        
        "-I${PROJECT_SOURCE_DIR}/src/common.h"
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

set(PLUGIN_DEFINITIONS
    CPLUG_COMPANY_NAME="${COMPANY_NAME}"
    CPLUG_COMPANY_EMAIL="contact@exacoustics.com"
    CPLUG_PLUGIN_NAME="${PROJECT_NAME}"
    CPLUG_PLUGIN_URI="${PROJECT_HOMEPAGE_URL}"
    CPLUG_PLUGIN_VERSION="${PROJECT_VERSION}"
    _HAS_EXCEPTIONS=0
    )
if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    list(APPEND PLUGIN_DEFINITIONS DEBUG=1)
endif()

set(PLUGIN_SOURCES
    src/plugin.c
    src/gui.c

    src/libs/nanovg.c
    src/libs/nanovg_sokol.c
    )

set(PLUGIN_INCLUDE
    modules/xhl/include
    modules/CPLUG/src/

    src/libs
    build/shaders/
    )

set(PLUGIN_LIBRARIES "")
set(SHADER_LANG "")

if (WIN32)
    list(APPEND PLUGIN_DEFINITIONS UNICODE _UNICODE PW_DX11)
    set(SHADER_LANG "hlsl5")

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
    list(APPEND PLUGIN_SOURCES src/platform_win.c)
    list(APPEND PLUGIN_LIBRARIES dxguid)
elseif(APPLE)
    enable_language(OBJC)
    set(SHADER_LANG "metal_macos")
    if (CMAKE_BUILD_TYPE MATCHES Release)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
        list(APPEND PLUGIN_OPTIONS -g)
    endif()
    
    list(APPEND PLUGIN_SOURCES src/platform_osx.m)
    list(APPEND PLUGIN_LIBRARIES
        "-framework Quartz"
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit")
    list(APPEND PLUGIN_DEFINITIONS
        PW_METAL)
endif()


set(COMPILE_SHADER_CMD sokol-shdc --input ${PROJECT_SOURCE_DIR}/src/libs/nanovg_sokol.glsl --output ${CMAKE_CURRENT_BINARY_DIR}/shaders/nanovg_sokol.glsl.h --slang ${SHADER_LANG})

### COMPILE SHADERS ###
# https://discourse.cmake.org/t/pre-ninja-bash-command/1418/5
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/shader.h.noexist
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/nanovg_sokol.glsl.h
    COMMAND
        ${COMPILE_SHADER_CMD}
)
add_custom_target(shader_header
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/shaders/nanovg_sokol.glsl.h
)


# ███████╗████████╗ █████╗ ███╗   ██╗██████╗  █████╗ ██╗      ██████╗ ███╗   ██╗███████╗
# ██╔════╝╚══██╔══╝██╔══██╗████╗  ██║██╔══██╗██╔══██╗██║     ██╔═══██╗████╗  ██║██╔════╝
# ███████╗   ██║   ███████║██╔██╗ ██║██║  ██║███████║██║     ██║   ██║██╔██╗ ██║█████╗  
# ╚════██║   ██║   ██╔══██║██║╚██╗██║██║  ██║██╔══██║██║     ██║   ██║██║╚██╗██║██╔══╝  
# ███████║   ██║   ██║  ██║██║ ╚████║██████╔╝██║  ██║███████╗╚██████╔╝██║ ╚████║███████╗
# ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝

if (WIN32)
    add_executable(${PROJECT_NAME}_standalone WIN32 ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_win.c)
    target_include_directories(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_INCLUDE})
    target_link_libraries(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_STANDALONE)
    target_compile_options(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_OPTIONS})
    add_dependencies(${PROJECT_NAME}_standalone shader_header)
elseif(APPLE)
    add_executable(${PROJECT_NAME}_app MACOSX_BUNDLE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_osx.m)
    target_include_directories(${PROJECT_NAME}_app PRIVATE ${PLUGIN_INCLUDE})
    set_target_properties(${PROJECT_NAME}_app PROPERTIES
        BUNDLE True
        OUTPUT_NAME ${PROJECT_NAME}                                                                # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app # out directory name, differs from target name
        MACOSX_BUNDLE_GUI_IDENTIFIER com.${COMPANY_NAME}.${PROJECT_NAME}.app
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT ${COMPANY_NAME}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/assets/app.plist
    )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo "APPL????")
    target_link_libraries(${PROJECT_NAME}_app PRIVATE ${PLUGIN_LIBRARIES} "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework CoreServices")
    target_compile_definitions(${PROJECT_NAME}_app PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_STANDALONE)
    target_compile_options(${PROJECT_NAME}_app PRIVATE ${PLUGIN_OPTIONS})
    add_dependencies(${PROJECT_NAME}_app shader_header)
endif()


# ██╗   ██╗███████╗████████╗██████╗ 
# ██║   ██║██╔════╝╚══██╔══╝╚════██╗
# ██║   ██║███████╗   ██║    █████╔╝
# ╚██╗ ██╔╝╚════██║   ██║    ╚═══██╗
#  ╚████╔╝ ███████║   ██║   ██████╔╝
#   ╚═══╝  ╚══════╝   ╚═╝   ╚═════╝ 

add_library(${PROJECT_NAME}_vst3 MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_vst3.c)
target_include_directories(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_INCLUDE})
target_link_libraries(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_LIBRARIES})
target_compile_definitions(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_VST3)
target_compile_options(${PROJECT_NAME}_vst3 PRIVATE ${PLUGIN_OPTIONS})
if (WIN32)
    set_target_properties(${PROJECT_NAME}_vst3 PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        SUFFIX .vst3
        PDB_NAME ${PROJECT_NAME}_vst3
    )
elseif (APPLE)
    set_target_properties(${PROJECT_NAME}_vst3 PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION vst3
        OUTPUT_NAME ${PROJECT_NAME}                                                                 # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3 # out directory name, differs from target name
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER com.${COMPANY_NAME}.${PROJECT_NAME}.vst3
        MACOSX_BUNDLE_COPYRIGHT ${COMPANY_NAME}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/assets/vst3.plist
    )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3/Contents/PkgInfo "BNDL????")
endif()

#  ██████╗██╗      █████╗ ██████╗
# ██╔════╝██║     ██╔══██╗██╔══██╗
# ██║     ██║     ███████║██████╔╝
# ██║     ██║     ██╔══██║██╔═══╝
# ╚██████╗███████╗██║  ██║██║
#  ╚═════╝╚══════╝╚═╝  ╚═╝╚═╝

add_library(${PROJECT_NAME}_clap MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_clap.c)
target_include_directories(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_INCLUDE})
target_link_libraries(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_LIBRARIES})
target_compile_definitions(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_CLAP)
target_compile_options(${PROJECT_NAME}_clap PRIVATE ${PLUGIN_OPTIONS})

if (WIN32)
    set_target_properties(${PROJECT_NAME}_clap PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
        SUFFIX .clap
        PDB_NAME ${PROJECT_NAME}_clap
        )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME}_clap PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION clap
        OUTPUT_NAME ${PROJECT_NAME}                                                                 # out binary name, differs from target name
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap # out directory name, differs from target name
        MACOSX_BUNDLE_GUI_IDENTIFIER com.${COMPANY_NAME}.${PROJECT_NAME}.clap
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT ${COMPANY_NAME}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/assets/clap.plist
        )
    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo)
    file(WRITE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo "BNDL????")
endif()

#  █████╗ ██╗   ██╗██╗   ██╗██████╗ 
# ██╔══██╗██║   ██║██║   ██║╚════██╗
# ███████║██║   ██║██║   ██║ █████╔╝
# ██╔══██║██║   ██║╚██╗ ██╔╝██╔═══╝ 
# ██║  ██║╚██████╔╝ ╚████╔╝ ███████╗
# ╚═╝  ╚═╝ ╚═════╝   ╚═══╝  ╚══════╝

# Read the comments here carefully. Building Audio Units v2 through CMake is not as easy as VST3 or CLAP

if (APPLE)

add_library(${PROJECT_NAME}_auv2 MODULE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_auv2.c)
target_include_directories(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_INCLUDE})
target_link_libraries(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_LIBRARIES} "-framework AudioToolbox") # -framework AudioToolbox may not be required...
target_compile_options(${PROJECT_NAME}_auv2 PRIVATE ${PLUGIN_OPTIONS})

# The following properties will end up in our bundles .plist, but must be set here.
# CMake doesn't support passing them as arguments in 'set_target_properties' below
# Some of these properties are used by both the .plist, and inside the AU when the host requests a 'ClassInfo' dict
# I would love to only define these in example/config.h, but then I would be duplicating these properties.
# Instead, I configure all AU settings from a single place
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.${COMPANY_NAME}.${PROJECT_NAME}.auv2")
set(MACOSX_BUNDLE_DESCRIPTION ${PROJECT_NAME})
set(MACOSX_BUNDLE_TAGS "<string>Synthesizer</string>")
set(MACOSX_BUNDLE_TYPE "aumu")
set(MACOSX_BUNDLE_SUBTYPE "RIFF")
set(MACOSX_BUNDLE_MANUFACTURER "ExAc")
# Convert version string (1.0.0) > integer (65536)
math(EXPR MACOSX_BUNDLE_VERSION_INT "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT DECIMAL) # cool trick bro

set_target_properties(${PROJECT_NAME}_auv2 PROPERTIES
    BUNDLE True
    BUNDLE_EXTENSION component
    OUTPUT_NAME ${PROJECT_NAME}                                                                      # out binary name, differs from target name
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component # out directory name, differs from target name
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT ${COMPANY_NAME}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/assets/auv2.plist
    )
file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component/Contents/PkgInfo)
file(WRITE ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component/Contents/PkgInfo "BNDL????")

target_compile_definitions(${PROJECT_NAME}_auv2 PRIVATE
    ${PLUGIN_DEFINITIONS}
    CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
    CPLUG_AUV2_BUNDLE_ID="${MACOSX_BUNDLE_GUI_IDENTIFIER}"
    CPLUG_BUILD_AUV2
)
add_dependencies(${PROJECT_NAME}_auv2 shader_header)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_custom_command(TARGET ${PROJECT_NAME}_auv2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component to ~/Library/Audio/Plug-Ins/Components/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component" "~/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component"
        )
endif() # Debug
endif() # APPLE
