cmake_minimum_required(VERSION 3.25)

include(Scripts/Windows.cmake)

set(CMAKE_CXX_STANDARD 17)

project(Gorgon 
    VERSION 4.2.0
    DESCRIPTION "Gorgon is a cross-platform framework for games and visual applications. \
        It provides a powerful and flexible set of tools for creating high-performance graphics, \
        audio, and input handling. Gorgon is designed to be easy to use and highly customizable."
    LANGUAGES CXX C
)
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/Scripts")

include(CMakeSetup)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(Gorgon STATIC "")
add_library(Gorgon::Gorgon ALIAS Gorgon)
target_compile_features(Gorgon PUBLIC cxx_std_17)

include(Options)
include(Compiler)
include(Functions)

add_subdirectory(Tools/ShaderEmbedder)

add_subdirectory(Source/Gorgon)
get_target_property(GORGON_SOURCES Gorgon SOURCES)
get_target_property(GORGON_HEADERS Gorgon HEADER_SET_HEADERS)

if(TESTS)
    include(Tests)
endif()

include(Documentation)
include(Install)

target_include_directories(Gorgon PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/Source>
    $<INSTALL_INTERFACE:include> 
)

include(Dependencies)

if(BUILD_EDITOR)
    add_subdirectory(Tools/Editor)
endif()

if(BUILD_EXAMPLES)
    include(Scripts/Public.cmake)
    add_subdirectory(Examples)
endif()

if(MSVC)
    include(SourceTree)
endif()

# ==============================================================================
# CPack: a distributable for the editor tools
# ==============================================================================
# `cpack -C Release` in the build directory produces a ZIP (and, when NSIS is installed, an
# installer) holding GorgonEditor, GorgonPlayer and the runtime DLLs that must sit beside them.
# Only the EditorTools component is packed, so the framework's SDK install (headers, libs, docs,
# desktop shortcut) is not pulled into the archive. Nothing here runs during a normal build.
if(BUILD_EDITOR)
    install(TARGETS GorgonEditor GorgonPlayer
        RUNTIME DESTINATION "."
        COMPONENT EditorTools
    )

    # The bundle is portable only while the runtime DLLs stay next to the executables, so ship
    # every DLL the build placed beside them (vulkan-1.dll and friends).
    install(CODE "
        file(GLOB _gorgon_runtime_dlls \"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/*.dll\")
        foreach(_gorgon_dll \${_gorgon_runtime_dlls})
            file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE FILE FILES \"\${_gorgon_dll}\")
        endforeach()
    " COMPONENT EditorTools)

    set(CPACK_GENERATOR "ZIP")
    # NSIS is optional: a missing makensis must never break a configure or a build.
    find_program(MAKENSIS_EXECUTABLE NAMES makensis)
    if(WIN32 AND MAKENSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
    endif()

    set(CPACK_PACKAGE_NAME "GorgonEditor")
    set(CPACK_PACKAGE_VENDOR "Gorgon")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Gorgon 2D editor and standalone game player")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "GorgonEditor")

    # One archive per component: with only EditorTools listed, that is a single archive holding
    # the binaries. (A single non-component package would also install the framework SDK rules.)
    set(CPACK_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_ALL EditorTools)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENT_EDITORTOOLS_DISPLAY_NAME "Gorgon Editor and Player")

    set(CPACK_NSIS_DISPLAY_NAME "Gorgon 2D Editor")
    set(CPACK_NSIS_PACKAGE_NAME "Gorgon 2D Editor")
    set(CPACK_NSIS_COMPONENT_INSTALL ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)

    include(CPack)
endif()
