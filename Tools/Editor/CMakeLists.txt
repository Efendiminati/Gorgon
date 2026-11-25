cmake_minimum_required(VERSION 3.25)

add_library(GorgonEditorCore STATIC
    Core/Components.h
    Core/Entity.h
    Core/RuntimeCamera.h
    Core/RuntimeCamera.cpp
    Core/Scene.h
    Core/Scene.cpp
    Core/SceneSerializer.h
    Core/SceneSerializer.cpp
    Core/CommandStack.h
    Core/CommandStack.cpp
    Core/CollisionSystem.h
    Core/CollisionSystem.cpp
    Core/ComponentSimulation.h
    Core/ComponentSimulation.cpp
    Core/TilemapPainter.h
    Core/TilemapPainter.cpp
    Core/AnimTimeline.h
    Core/AnimTimeline.cpp
    Core/Lightmap.h
    Core/Lightmap.cpp
    Core/InputState.h
    Core/ScriptLog.h
    Core/ScriptLog.cpp
    Core/FileWatcher.h
    Core/FileWatcher.cpp
    Core/GameExporter.h
    Core/GameExporter.cpp
    Core/PlaySession.h
    Core/PlaySession.cpp
    Core/SceneRuntime.h
    Core/SceneRuntime.cpp
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
add_subdirectory(Player)

if(TESTS AND UNIT_TESTS AND TARGET Catch2::Catch2WithMain)
    add_subdirectory(Tests)
endif()
