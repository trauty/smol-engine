set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

set(COMMON_LINKER_FLAGS "-fuse-ld=lld -nostdlib++ --rtlib=compiler-rt -Wl,-Bstatic -lc++ -lc++abi ${PROJECT_ROOT}/lib/std/linux/libunwind.a -Wl,-Bdynamic -lpthread -ldl")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")

set(CMAKE_CXX_FLAGS_DEBUG "-g -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-rdynamic")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "-rdynamic")
set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "-rdynamic")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffunction-sections -fdata-sections")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")

set(CPACK_GENERATOR "TGZ")