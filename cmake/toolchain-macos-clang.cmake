# Deprecated (dont have an apple device to test, is definitely broken)

set(CMAKE_SYSTEM_NAME Darwin)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lc++")

set(CPACK_GENERATOR "TGZ")