cmake_minimum_required(VERSION 3.26)
project(smol-engine VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX true)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")

if(LINUX)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    message("IPO/LTO is supported.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DISTRIBUTION ON)
else()
    message("IPO/LTO is not supported: ${ipo_error}")
endif()

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

SET_INTERPROCEDURAL_OPTIMIZATION()

set(SMOL_ENGINE_NAME "smol-engine")

set(SMOL_ENGINE_SOURCES
    
    # Glad
	lib/glad/gl.c

    # stb
    lib/stb/stb_image.cpp
    lib/stb/stb_image_write.cpp

    # tinygltf
    lib/tinygltf/tiny_gltf.cpp

    # Engine
	src/engine.cpp
	src/window.cpp
	src/log.cpp
	src/util.cpp
	src/time_util.cpp
	src/main_thread.cpp
	src/events.cpp
	src/physics.cpp

    src/core/component.cpp
    src/core/gameobject.cpp
    src/core/level.cpp

    src/components/test_component.cpp
    src/components/transform.cpp
    src/components/mesh_renderer.cpp
    src/components/camera.cpp
    src/components/renderer_component.cpp
    src/components/collider.cpp
	src/components/box_collider.cpp
	src/components/capsule_collider.cpp
	src/components/rigidbody.cpp

    src/asset/asset_manager.cpp
    src/asset/texture.cpp
    src/asset/shader.cpp
    src/asset/mesh.cpp

    src/rendering/renderer.cpp
    src/rendering/material.cpp
    src/rendering/spatial_material.cpp

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

if(WIN32)
    install(FILES "${SDL3_BASE_PATH}/SDL3.dll" DESTINATION .)
elseif(LINUX)
    install(FILES "${SDL3_BASE_PATH}/libSDL3.so.0" DESTINATION .)
    install(FILES "${SDL3_BASE_PATH}/libSDL3.so.0.2.18" DESTINATION .)
endif()

install(FILES "$<TARGET_FILE:${SMOL_ENGINE_NAME}>" DESTINATION .)

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