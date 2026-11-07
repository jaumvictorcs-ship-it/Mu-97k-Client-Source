# Mu CLT Launcher: projeto separado do mu97k (o CI compara flags msbuild x cmake so do cliente).
# Build: cmake -S launcher -B build-launcher -A Win32 -T v143 && cmake --build build-launcher --config Release
cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(MuCLTLauncher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# /MT: roda em Windows 7/10/11 e Wine sem VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(launcher_core STATIC Updater.cpp Updater.h PublicKey.h)
target_compile_definitions(launcher_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
target_compile_options(launcher_core PUBLIC /W4 /utf-8)
target_link_libraries(launcher_core PUBLIC winhttp bcrypt)

add_executable(Launcher WIN32 Launcher.cpp Launcher.rc)
target_link_libraries(Launcher PRIVATE launcher_core comctl32 msimg32 windowscodecs ole32)
set_target_properties(Launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# O manifest vai pelo .rc; o linker nao gera outro.
target_link_options(Launcher PRIVATE /MANIFEST:NO)

add_executable(launcher_selftest SelfTest.cpp)
target_link_libraries(launcher_selftest PRIVATE launcher_core)
