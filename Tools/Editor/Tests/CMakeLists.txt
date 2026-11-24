cmake_minimum_required(VERSION 3.25)

add_executable(UnitTest-EditorCore
    EditorCoreTests.cpp
)

target_link_libraries(UnitTest-EditorCore PRIVATE
    GorgonEditorCore
    Catch2::Catch2WithMain
)

set_target_properties(UnitTest-EditorCore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Unit"
)

add_test(NAME EditorCore
    COMMAND $<TARGET_FILE:UnitTest-EditorCore>
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/Unit"
)
