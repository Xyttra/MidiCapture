cmake_minimum_required(VERSION 3.22)
project(MidiCapture VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch CLAP SDK headers
include(FetchContent)
FetchContent_Declare(
    clap
    URL https://github.com/free-audio/clap/archive/refs/tags/1.2.2.zip
)
FetchContent_MakeAvailable(clap)

# Plugin shared library
add_library(MidiCapture MODULE
    src/entry.cpp
    src/plugin.cpp
    src/gui.cpp
    src/midi_export.cpp
)

target_include_directories(MidiCapture PRIVATE
    ${clap_SOURCE_DIR}/include
    src
)

# Windows-specific settings
if(WIN32)
    target_link_libraries(MidiCapture PRIVATE user32 gdi32)
    set_target_properties(MidiCapture PROPERTIES
        SUFFIX ".clap"
        PREFIX ""
    )
endif()

# Disable C++ exceptions and RTTI for smaller binary
target_compile_definitions(MidiCapture PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Install (Post-build)
set(CLAP_USER_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
add_custom_command(TARGET MidiCapture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_USER_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MidiCapture>" "${CLAP_USER_DIR}/MidiCapture.clap"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:MidiCapture>" "${CMAKE_SOURCE_DIR}/MidiCapture.clap"
    COMMENT "Installing MidiCapture.clap to ${CLAP_USER_DIR} and workspace"
)
