# this needs to be revamped

set(CMAKE_SYSTEM_NAME Windows)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_FLAGS_INIT "-flto -Wall")
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++ -flto -Wall")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -flto -static-libc++ -lc++ -lc++abi -lunwind")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -flto -static-libc++ -lc++ -lc++abi -lunwind")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -flto -static-libc++ -lc++ -lc++abi -lunwind")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CPACK_GENERATOR "ZIP")