cmake_minimum_required(VERSION 3.25)

add_library(GorgonEditorCore STATIC
    Core/Components.h
    Core/Entity.h
    Core/Scene.h
    Core/Scene.cpp
    Core/SceneSerializer.h
    Core/SceneSerializer.cpp
)
add_library(Gorgon::EditorCore ALIAS GorgonEditorCore)

target_compile_features(GorgonEditorCore PUBLIC cxx_std_17)

target_include_directories(GorgonEditorCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(GorgonEditorCore PUBLIC
    Gorgon::Gorgon
)

add_subdirectory(App)

if(TESTS AND UNIT_TESTS AND TARGET Catch2::Catch2WithMain)
    add_subdirectory(Tests)
endif()
