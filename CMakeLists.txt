cmake_minimum_required(VERSION 3.26)
project(smol-engine VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX true)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpmcache" CACHE STRING "CPM cache")

include(cmake/CPM.cmake)

find_package(Threads REQUIRED)

CPMAddPackage(
    URI "gh:libsdl-org/SDL#release-3.2.16"
    OPTIONS "SDL_AUDIO_DEFAULT OFF" 
    "SDL_GPU_DEFAULT OFF"
    "SDL_RENDER_DEFAULT OFF"
    "SDL_CAMERA_DEFAULT OFF"
    "SDL_JOYSTICK_DEFAULT OFF"
    "SDL_HAPTIC_DEFAULT OFF"
    "SDL_POWER_DEFAULT OFF"
    "SDL_SENSOR_DEFAULT OFF"
    "SDL_DIALOG_DEFAULT OFF"
    "SDL_TEST_LIBRARY OFF"
    "SDL_OPENGLES OFF"
    "SDL_OPENGL ON"
    "SDL_VULKAN OFF"
    "SDL_DIRECTX OFF"
)

add_definitions(-DCGLM_FORCE_LEFT_HANDED)

CPMAddPackage(URI "gh:recp/cglm#v0.9.6" OPTIONS "CGLM_STATIC ON")

CPMAddPackage(URI "gh:fmtlib/fmt#11.2.0" OPTIONS "CMAKE_POSITION_INDEPENDENT_CODE ON")

CPMAddPackage(URI "gh:syoyo/tinygltf#v2.9.6" OPTIONS "CMAKE_POSITION_INDEPENDENT_CODE ON" "INTERPROCEDURAL_OPTIMIZATION OFF")

CPMAddPackage(
  	NAME JoltPhysics
  	GITHUB_REPOSITORY jrouwe/JoltPhysics
  	GIT_TAG v5.3.0
	OPTIONS 
	"CMAKE_POSITION_INDEPENDENT_CODE ON"
	"JPH_ENABLE_ASSERTS OFF"
    "JPH_ENABLE_DEBUG_RENDERER OFF"
    "JPH_PROFILE_ENABLED OFF"
    "JPH_ENABLE_TRACY_PROFILING OFF"

	SOURCE_SUBDIR "Build"
)

CPMAddPackage(
  NAME miniaudio
  GITHUB_REPOSITORY mackron/miniaudio
  GIT_TAG 0.11.22
  DOWNLOAD_ONLY TRUE
)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})

set(SMOL_ENGINE_NAME "smol-engine")

set(SMOL_ENGINE_SOURCES
	lib/glad/gl.c
	src/engine/engine.cpp
	src/engine/window.cpp
	src/engine/log.cpp
	src/engine/util.cpp
	src/engine/time_util.cpp
	src/engine/main_thread.cpp
	src/engine/events.cpp
	src/engine/physics.cpp

    src/engine/core/component.cpp
    src/engine/core/gameobject.cpp
    src/engine/core/level.cpp

    src/engine/components/test_component.cpp
    src/engine/components/transform.cpp
    src/engine/components/mesh_renderer.cpp
    src/engine/components/camera.cpp
    src/engine/components/renderer_component.cpp
    src/engine/components/collider.cpp
	src/engine/components/box_collider.cpp
	src/engine/components/capsule_collider.cpp
	src/engine/components/rigidbody.cpp

    src/engine/asset/asset_manager.cpp
    src/engine/asset/texture.cpp
    src/engine/asset/shader.cpp
    src/engine/asset/mesh.cpp

    src/engine/rendering/renderer.cpp
    src/engine/rendering/material.cpp
    src/engine/rendering/spatial_material.cpp

)

# Engine Library
add_library(${SMOL_ENGINE_NAME} SHARED ${SMOL_ENGINE_SOURCES})
set_target_properties(${SMOL_ENGINE_NAME} PROPERTIES PREFIX "")
if(LINUX)
    set_target_properties(${SMOL_ENGINE_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "\$ORIGIN"
        SKIP_BUILD_RPATH FALSE
    )
endif()

target_compile_features(${SMOL_ENGINE_NAME} PUBLIC cxx_std_20)
target_compile_definitions(${SMOL_ENGINE_NAME} PRIVATE SMOL_EXPORT)

if(WIN32)
    target_link_libraries(${SMOL_ENGINE_NAME} PUBLIC
	    SDL3::SDL3
	    Threads::Threads
	    cglm
	    fmt
	    tinygltf
	    Jolt
        miniaudio
	    kernel32
    )
else()
    target_link_libraries(${SMOL_ENGINE_NAME} PUBLIC
	    SDL3::SDL3
	    Threads::Threads
	    cglm
	    fmt
	    tinygltf
	    Jolt
        miniaudio
    )
endif()

target_include_directories(${SMOL_ENGINE_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/src/engine 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Install
install(
    TARGETS ${SMOL_GAME_NAME}
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION lib
)

install(FILES "$<TARGET_FILE:SDL3::SDL3>" DESTINATION .)
install(FILES "$<TARGET_FILE:${SMOL_ENGINE_NAME}>" DESTINATION .)

if(LINUX)
    install(CODE "
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink libSDL3.so.0.2.16 libSDL3.so.0
        WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}/lib
    )"
)
endif()

if(LINUX)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/lib/linux/ DESTINATION .)
endif()

#install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets DESTINATION .)

# Copy assets folder
#add_custom_target(copy_assets
    #COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different 
            #${CMAKE_SOURCE_DIR}/assets
            #${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    #COMMENT "Copying assets to build output directory"
#)

#add_dependencies(${SMOL_GAME_NAME} copy_assets)

# CPack
#set(CPACK_PROJECT_NAME ${SMOL_GAME_NAME})
#set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
#set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
#set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
#include(CPack)

#include(CTest)
#enable_testing()
#add_test(NAME all_tests COMMAND unit_tests)