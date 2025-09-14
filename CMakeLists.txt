cmake_minimum_required(VERSION 3.11)
project(doom_instancer)

set(CMAKE_CXX_STANDARD 20)

add_executable(doom_instancer WIN32
	src/main.cxx
	imgui/imgui.cpp
	imgui/imgui_demo.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/backends/imgui_impl_sdl2.cpp
	imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(doom_instancer PRIVATE include)
target_include_directories(doom_instancer PRIVATE imgui)
target_include_directories(doom_instancer PRIVATE imgui/backends)

find_package(SDL2 REQUIRED)
if (WIN32)
	target_link_libraries(doom_instancer PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(doom_instancer PRIVATE SDL2::SDL2)

find_package(CURL REQUIRED)
target_link_libraries(doom_instancer PRIVATE CURL::libcurl)

set(OpenGL_GL_PREFERENCE "GLVND")
find_package(OpenGL REQUIRED)
target_link_libraries(doom_instancer PRIVATE OpenGL::GL)
target_link_libraries(doom_instancer PRIVATE OpenGL::GLU)

find_package(GLEW REQUIRED)
target_link_libraries(doom_instancer PRIVATE GLEW::GLEW)

add_subdirectory(portable-file-dialogs)
target_link_libraries(doom_instancer PRIVATE portable_file_dialogs)

include(FetchContent)
FetchContent_Declare(
	json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)
target_link_libraries(doom_instancer PRIVATE nlohmann_json::nlohmann_json)

configure_file(Jupiter.ttf Jupiter.ttf COPYONLY)
configure_file(imgui/misc/fonts/ProggyClean.ttf ProggyClean.ttf COPYONLY)
configure_file(imgui/misc/fonts/ProggyTiny.ttf ProggyTiny.ttf COPYONLY)
