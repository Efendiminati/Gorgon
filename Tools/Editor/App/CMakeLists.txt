cmake_minimum_required(VERSION 3.25)

add_executable(GorgonEditor
    EditorTheme.h
    EditorTheme.cpp
    Viewport.h
    Viewport.cpp
    EditorApp.h
    EditorApp.cpp
    Main.cpp
)

target_compile_features(GorgonEditor PRIVATE cxx_std_17)

target_link_libraries(GorgonEditor PRIVATE
    Gorgon::Gorgon
    Gorgon::EditorCore
)
