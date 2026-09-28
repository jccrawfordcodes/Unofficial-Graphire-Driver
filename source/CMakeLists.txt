cmake_minimum_required(VERSION 3.20)
project(GraphireDriver C RC)

add_executable(GraphireDriver WIN32
    src/main.c src/device.c src/output.c src/graphire.c
    res/app.rc)

target_compile_definitions(GraphireDriver PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(GraphireDriver PRIVATE hid setupapi shell32 user32 gdi32 advapi32)

if(MSVC)
    # the manifest (Per-Monitor DPI awareness) is embedded by app.rc
    target_link_options(GraphireDriver PRIVATE /MANIFEST:NO)
    # static CRT: the exe runs without the VC++ redistributable
    set_property(TARGET GraphireDriver PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(GraphireDriver PRIVATE /W3)
else()
    target_link_options(GraphireDriver PRIVATE -municode -static)
endif()
