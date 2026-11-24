cmake_minimum_required(VERSION 3.25)

project(EditorDemo LANGUAGES CXX)

# Only enable Debug and Release builds
set(CMAKE_CONFIGURATION_TYPES Debug Release)

if(NOT TARGET Gorgon::Gorgon)
    find_package(Gorgon REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(${PROJECT_NAME} WIN32
    Main.cpp
)

# GorgonEditorCore carries the scene model, the JSON serializer and the runtime scene loader.
# It only exists when the framework was configured with BUILD_EDITOR=ON, which is why
# Examples/CMakeLists.txt adds this directory conditionally.
target_link_libraries(${PROJECT_NAME} PRIVATE
    Gorgon::Gorgon
    Gorgon::EditorCore
)

# Keep the demo beside the other samples during development.
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/Bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/Bin"
)

# The scene folder (DemoScene.gscene.json, assets/, scripts/) is copied next to the executable so
# the demo can be started straight from the build output.
gorgon_copy_directory(${PROJECT_NAME} "Scene")

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "DarkGaze.Org")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
