cmake_minimum_required(VERSION 3.15)
project(forebay_library C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(spisor_library
    GIT_REPOSITORY https://github.com/spisor/library.git
    GIT_TAG 4df0ab909692aad80433c26ce22cf4f035921258)
FetchContent_MakeAvailable(spisor_library)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(apps SHARED
    src/apps.c
    src/current.c
    src/descriptor.c
    src/environment.c
    src/home.c
    src/javascript.c
    src/registry.c
    src/support.c)
target_link_libraries(apps PRIVATE spisor_library)
set_target_properties(apps PROPERTIES C_VISIBILITY_PRESET hidden)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(apps PRIVATE -Wl,--exclude-libs,ALL)
endif()
spisor_strict_warnings(apps)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(test)
endif()
