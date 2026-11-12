cmake_minimum_required(VERSION 3.15)
project(forebay_library C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/siblings/library/CMakeLists.txt)
    message(FATAL_ERROR "siblings/library is missing: check out the dependency .weivel/repo.json declares")
endif()
add_subdirectory(siblings/library)

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
    src/release.c
    src/routing_arrival.c
    src/routing_environment.c
    src/routing_failure.c
    src/routing_headers.c
    src/routing_limits.c
    src/routing_model_map.c
    src/routing_outcome.c
    src/routing_pattern.c
    src/routing_resolve.c
    src/routing_step.c
    src/routing_step_answer.c
    src/routing_step_walk.c
    src/routing_support.c
    src/routing_tiers.c
    src/routing_upstream.c
    src/service_support.c)
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
