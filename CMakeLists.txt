cmake_minimum_required(VERSION 3.26)
project(smol-engine VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX true)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(LINUX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    message("IPO/LTO is supported. ${ipo_error}")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DISTRIBUTION ON)
else()
    message("IPO/LTO is not supported: ${ipo_error}")
endif()

set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.cpmcache" CACHE STRING "CPM cache")
include(cmake/CPM.cmake)

find_package(Threads REQUIRED)

CPMAddPackage(
    URI "gh:libsdl-org/SDL#release-3.2.24"
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
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
)

add_definitions(-DCGLM_FORCE_LEFT_HANDED)

CPMAddPackage(URI "gh:recp/cglm#v0.9.6" OPTIONS "CGLM_STATIC ON")

CPMAddPackage(URI "gh:fmtlib/fmt#12.0.0" OPTIONS "CMAKE_POSITION_INDEPENDENT_CODE ON")

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

set(SMOL_ENGINE_NAME "smol-engine")
set(SMOL_ENGINE_SRC_DIR "src/smol")

set(SMOL_ENGINE_SOURCES
    
    # Glad
	lib/glad/gl.c

    # stb
    lib/stb/stb_image.cpp
    lib/stb/stb_image_write.cpp

    # tinygltf
    lib/tinygltf/tiny_gltf.cpp

    # engine
	${SMOL_ENGINE_SRC_DIR}/engine.cpp
	${SMOL_ENGINE_SRC_DIR}/window.cpp
	${SMOL_ENGINE_SRC_DIR}/log.cpp
	${SMOL_ENGINE_SRC_DIR}/util.cpp
	${SMOL_ENGINE_SRC_DIR}/time_util.cpp
	${SMOL_ENGINE_SRC_DIR}/main_thread.cpp
	${SMOL_ENGINE_SRC_DIR}/events.cpp
	${SMOL_ENGINE_SRC_DIR}/physics.cpp

    ${SMOL_ENGINE_SRC_DIR}/core/component.cpp
    ${SMOL_ENGINE_SRC_DIR}/core/gameobject.cpp
    ${SMOL_ENGINE_SRC_DIR}/core/level.cpp

    ${SMOL_ENGINE_SRC_DIR}/components/transform.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/mesh_renderer.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/camera.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/renderer_component.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/box_collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/capsule_collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/rigidbody.cpp

    ${SMOL_ENGINE_SRC_DIR}/asset/asset_manager.cpp
    ${SMOL_ENGINE_SRC_DIR}/asset/texture.cpp
    ${SMOL_ENGINE_SRC_DIR}/asset/shader.cpp
    ${SMOL_ENGINE_SRC_DIR}/asset/mesh.cpp

    ${SMOL_ENGINE_SRC_DIR}/rendering/renderer.cpp
    ${SMOL_ENGINE_SRC_DIR}/rendering/material.cpp
    ${SMOL_ENGINE_SRC_DIR}/rendering/spatial_material.cpp

)

# engine lib
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
target_compile_options(${SMOL_ENGINE_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types -Wno-missing-field-initializers)

target_link_libraries(
    ${SMOL_ENGINE_NAME} PUBLIC
	Threads::Threads
    SDL3::SDL3
	fmt
    cglm
	Jolt
	${SMOL_ADDITIONAL_SYS_LIBS}
)

target_include_directories(${SMOL_ENGINE_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Install
install(
    TARGETS ${SMOL_ENGINE_NAME}
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION lib
)

install(FILES "$<TARGET_FILE:SDL3::SDL3>" DESTINATION .)
install(FILES "$<TARGET_FILE:${SMOL_ENGINE_NAME}>" DESTINATION .)

#install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION .)

# Copy assets folder

add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different 
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    COMMENT "Copying assets to build output directory"
)

add_dependencies(${SMOL_ENGINE_NAME} copy_assets)

# CPack
#set(CPACK_PROJECT_NAME ${SMOL_GAME_NAME})
#set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
#set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
#set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
#include(CPack)

#include(CTest)
#enable_testing()
#add_test(NAME all_tests COMMAND unit_tests)