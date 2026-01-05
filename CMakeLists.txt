cmake_minimum_required(VERSION 3.26)
project(smol-engine VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX true)
endif()

include(cmake/policies.cmake)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

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
find_package(Vulkan REQUIRED)

CPMAddPackage(
    URI "gh:libsdl-org/SDL#release-3.2.28"
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
    "SDL_OPENGL OFF"
    "SDL_VULKAN ON"
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

find_library(
    VULKAN_LOADER_LIB 
    NAMES vulkan libvulkan
    HINTS "$ENV{VULKAN_SDK}"
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

set(VULKAN_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/vulkan")

set(SLANG_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/slang")
set(SLANG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/slang")

if(WIN32)
    set(VULKAN_PLATFORM_DIR "${VULKAN_VENDOR_DIR}/windows")
    set(VULKAN_LIB "${VULKAN_PLATFORM_DIR}/vulkan-1.dll")

    set(SLANG_PLATFORM_DIR "${SLANG_VENDOR_DIR}/windows")
    set(SLANG_LIB "${SLANG_PLATFORM_DIR}/slang.lib")

    set(
        SLANG_RUNTIME_FILES
        "${SLANG_PLATFORM_DIR}/slang.dll"
    )
elseif(LINUX)
    set(VULKAN_PLATFORM_DIR "${VULKAN_VENDOR_DIR}/linux")
    set(VULKAN_LIB "${VULKAN_PLATFORM_DIR}/libvulkan.so.1")

    set(SLANG_PLATFORM_DIR "${SLANG_VENDOR_DIR}/linux")
    set(SLANG_LIB "${SLANG_PLATFORM_DIR}/libslang.so")

    set(
        SLANG_RUNTIME_FILES
        "${SLANG_PLATFORM_DIR}/libslang.so"
    )
endif()

add_library(slang SHARED IMPORTED GLOBAL)

set_target_properties(slang PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SLANG_INCLUDE_DIR}"
)

if(WIN32)
    set_target_properties(slang PROPERTIES
        IMPORTED_IMPLIB "${SLANG_LIB}"
        IMPORTED_LOCATION "${SLANG_PLATFORM_DIR}/slang.dll"
    )
elseif(LINUX)
    set_target_properties(slang PROPERTIES
        IMPORTED_LOCATION "${SLANG_LIB}"
    )
endif()

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
	${SMOL_ENGINE_SRC_DIR}/asset.cpp

    ${SMOL_ENGINE_SRC_DIR}/core/component.cpp
    ${SMOL_ENGINE_SRC_DIR}/core/gameobject.cpp
    ${SMOL_ENGINE_SRC_DIR}/core/level.cpp

    ${SMOL_ENGINE_SRC_DIR}/components/transform.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/camera.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/renderer.cpp
    ${SMOL_ENGINE_SRC_DIR}/components/collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/box_collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/capsule_collider.cpp
	${SMOL_ENGINE_SRC_DIR}/components/rigidbody.cpp

    ${SMOL_ENGINE_SRC_DIR}/asset/texture.cpp
    ${SMOL_ENGINE_SRC_DIR}/asset/shader.cpp
    ${SMOL_ENGINE_SRC_DIR}/asset/mesh.cpp

    ${SMOL_ENGINE_SRC_DIR}/rendering/renderer.cpp
    ${SMOL_ENGINE_SRC_DIR}/rendering/material.cpp
    ${SMOL_ENGINE_SRC_DIR}/rendering/ubo.cpp
    ${SMOL_ENGINE_SRC_DIR}/rendering/shader_compiler.cpp

)

# engine lib
add_library(${SMOL_ENGINE_NAME} SHARED ${SMOL_ENGINE_SOURCES})
set_target_properties(${SMOL_ENGINE_NAME} PROPERTIES PREFIX "")
smol_set_rpath_policy(${PROJECT_NAME})

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
    Vulkan::Vulkan
	slang
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

# Copy assets folder

add_custom_target(
    copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different 
            ${CMAKE_SOURCE_DIR}/assets
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    COMMENT "Copying assets to build output directory"
)

add_custom_target(
    copy_vulkan_runtime_lib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${VULKAN_LIB}
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Copying Vulkan loader library to runtime directory"
)

add_custom_target(
    copy_slang_runtime_files
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SLANG_RUNTIME_FILES}
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Copying Slang runtime files to output directory"
)

add_dependencies(${SMOL_ENGINE_NAME} copy_assets copy_vulkan_runtime_lib copy_slang_runtime_files)

# CPack
#set(CPACK_PROJECT_NAME ${SMOL_GAME_NAME})
#set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
#set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
#set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
#include(CPack)