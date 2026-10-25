cmake_minimum_required(VERSION 3.21)

project(
    ImmersiveOutfitDistribution
    VERSION 0.3.3
    DESCRIPTION "Immersive Outfit Distribution: curated outfits sold by Skyrim vendors and granted at shrines."
    LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY)

# CommonLibSSE-NG (alandtse fork) as a submodule pinned to v12.1.0: reads the Address Library v5
# format used by Skyrim 1.7.99 / 1.7.104 and declares that compatibility.
include("${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR-ng/cmake/CommonLibSSE.cmake")
set(BUILD_TESTS OFF CACHE BOOL "Build unit tests for CommonLibVR." FORCE)
add_subdirectory(extern/CommonLibVR-ng)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(sources
    src/Main.cpp
    src/Catalog.cpp
    src/Requirements.cpp
    src/Sources.cpp
    src/Stocker.cpp
    src/State.cpp
    src/SetFiles.cpp
    src/Settings.cpp
    src/UI.cpp
    src/SetEdit.cpp
    src/Papyrus.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

set(headers
    src/PCH.h
    src/Catalog.h
    src/Requirements.h
    src/Sources.h
    src/Stocker.h
    src/State.h
    src/SetFiles.h
    src/Settings.h
    src/UI.h
    src/SetEdit.h
    src/Papyrus.h
    include/SKSEMenuFramework.h)

add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "sysss29"
    SOURCES ${headers} ${sources})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /sdl /utf-8 /Zi /EHsc /Zc:preprocessor /W4 /wd4200)
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif()
