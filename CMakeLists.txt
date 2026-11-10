cmake_minimum_required(VERSION 3.15)
project(forebay_library C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(spisor_library
    GIT_REPOSITORY https://github.com/spisor/library.git
    GIT_TAG 004a16b0e601327732dee63fea8f76fdfcc5d3a9)
FetchContent_MakeAvailable(spisor_library)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(forebay_library SHARED
    src/accounts_availability.c
    src/accounts_backoff.c
    src/accounts_manage.c
    src/accounts_quota.c
    src/accounts_report.c
    src/accounts_secrets.c
    src/accounts_select.c
    src/accounts_store.c
    src/accounts_support.c
    src/accounts_token.c
    src/apps.c
    src/current.c
    src/descriptor.c
    src/environment.c
    src/home.c
    src/javascript.c
    src/registry.c
    src/routing_environment.c
    src/routing_limits.c
    src/routing_model_map.c
    src/routing_outcome.c
    src/routing_pattern.c
    src/routing_resolve.c
    src/routing_support.c
    src/routing_tiers.c
    src/service_support.c
    src/support.c)
target_link_libraries(forebay_library PRIVATE spisor_library)
set_target_properties(forebay_library PROPERTIES C_VISIBILITY_PRESET hidden)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(forebay_library PRIVATE -Wl,--exclude-libs,ALL)
endif()
spisor_strict_warnings(forebay_library)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(test)
endif()
