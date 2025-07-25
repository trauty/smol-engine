cmake_minimum_required(VERSION 3.26)
project(smol-engine VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX true)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s")

find_package(Threads REQUIRED)

set(SMOL_ENGINE_NAME "smol-engine")

set(JPH_ENABLE_VIEWER OFF CACHE BOOL "" FORCE)
add_subdirectory(external/joltphysics/Build)

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

if(WIN32)
    set(SDL3_BASE_PATH "${CMAKE_SOURCE_DIR}/lib/SDL3/windows")
    set(SDL3_LIB_PATH "${SDL3_BASE_PATH}/libSDL3.dll.a")
    set(SDL3_DYNLIB_PATH "${SDL3_BASE_PATH}/SDL3.dll")

    set(FMT_BASE_PATH "${CMAKE_SOURCE_DIR}/lib/fmt/windows")
    set(FMT_LIB_PATH "${FMT_BASE_PATH}/libfmt.a")

    set(SMOL_ADDITIONAL_SYS_LIBS "kernel32")
endif()

target_link_libraries(
    ${SMOL_ENGINE_NAME} PUBLIC
	${SDL3_LIB_PATH}
	Threads::Threads
	${FMT_LIB_PATH}
	Jolt
	${SMOL_ADDITIONAL_SYS_LIBS}
)

target_include_directories(${SMOL_ENGINE_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/joltphysics
)

# Install
install(
    TARGETS ${SMOL_GAME_NAME}
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION lib
)

install(FILES "${SDL3_DYNLIB_PATH}" DESTINATION .)
install(FILES "$<TARGET_FILE:${SMOL_ENGINE_NAME}>" DESTINATION .)

if(LINUX)
    install(CODE "
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink libSDL3.so.0.2.16 libSDL3.so.0
        WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}/lib
    )"
)
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