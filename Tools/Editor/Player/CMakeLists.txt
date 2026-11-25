add_executable(GorgonPlayer WIN32
    Main.cpp
)

target_compile_features(GorgonPlayer PRIVATE cxx_std_17)

target_link_libraries(GorgonPlayer PRIVATE
    Gorgon::Gorgon
    Gorgon::EditorCore
)
