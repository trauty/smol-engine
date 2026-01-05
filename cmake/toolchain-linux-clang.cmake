set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VENDOR_LIB_DIR "${PROJECT_ROOT}/lib/std/linux")
set(VENDOR_INC_DIR "${PROJECT_ROOT}/include/std/c++/v1")
set(VENDOR_PLATFORM_INC_DIR "${PROJECT_ROOT}/include/std/x86_64-unknown-linux-gnu/c++/v1")

set(CMAKE_CXX_FLAGS_INIT 
    "-nostdlib++ -nostdinc++ -isystem \"${VENDOR_INC_DIR}\" -isystem \"${VENDOR_PLATFORM_INC_DIR}\""
)
set(COMMON_LINKER_FLAGS "-fuse-ld=lld -nostdlib++ --rtlib=compiler-rt -L\"${VENDOR_LIB_DIR}\"")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${COMMON_LINKER_FLAGS}")

set(CMAKE_CXX_STANDARD_LIBRARIES 
    "-nostdlib++ -lc++ -lc++abi -lunwind -lpthread -ldl"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "-fsanitize=address")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "-fsanitize=address")
set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "-fsanitize=address")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -ffunction-sections -fdata-sections")

set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections -s -flto")

set(CPACK_GENERATOR "TGZ")