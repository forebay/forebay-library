cmake_minimum_required(VERSION 3.19)
project(forebay_library C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(forebay_siblings ${CMAKE_CURRENT_SOURCE_DIR}/siblings)
if(NOT EXISTS ${forebay_siblings}/library/CMakeLists.txt)
    message(FATAL_ERROR "siblings/library is missing: check out the dependency repo.json declares")
endif()

include(${forebay_siblings}/library/cmake/sanitizers.cmake)
add_subdirectory(${forebay_siblings}/library ${PROJECT_BINARY_DIR}/siblings/library)
include(${forebay_siblings}/library/cmake/allocation_check.cmake)
include(${forebay_siblings}/library/cmake/link_graph.cmake)
spisor_refuse_plain_allocation(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(forebay_objects OBJECT
    src/accounts_availability.c
    src/accounts_backoff.c
    src/accounts_definition.c
    src/accounts_manage.c
    src/accounts_quota.c
    src/accounts_report.c
    src/accounts_secrets.c
    src/accounts_select.c
    src/accounts_store.c
    src/accounts_support.c
    src/accounts_token.c
    src/answer_storage.c
    src/anthropic_body.c
    src/anthropic_byte_buffer.c
    src/anthropic_check.c
    src/anthropic_decode_blocks.c
    src/anthropic_decode_request.c
    src/anthropic_decode_response.c
    src/anthropic_decode_stream.c
    src/anthropic_definition.c
    src/anthropic_encode_blocks.c
    src/anthropic_encode_request.c
    src/anthropic_encode_response.c
    src/anthropic_encode_stream.c
    src/anthropic_forwarded_headers.c
    src/anthropic_frames.c
    src/anthropic_rate_limit_reply.c
    src/anthropic_reader.c
    src/anthropic_refusal.c
    src/anthropic_request_answer.c
    src/anthropic_route.c
    src/anthropic_stop_reason.c
    src/anthropic_stream_decoder.c
    src/anthropic_stream_encoder.c
    src/anthropic_usage.c
    src/anthropic_wire.c
    src/anthropic_wire_shapes.c
    src/anthropic_writer.c
    src/definition_support.c
    src/routing_answers.c
    src/routing_arguments.c
    src/routing_arrival.c
    src/routing_definition.c
    src/routing_definition_step.c
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
target_include_directories(forebay_objects PUBLIC include PRIVATE src)
target_link_libraries(forebay_objects PUBLIC spisor_library)
target_compile_definitions(forebay_objects PRIVATE FOREBAY_LIBRARY_BUILDING)
set_target_properties(forebay_objects PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
spisor_strict_warnings(forebay_objects)

add_library(forebay_library SHARED src/library_load.c)
target_link_libraries(forebay_library PRIVATE forebay_objects spisor_allocator)
target_compile_definitions(forebay_library PRIVATE FOREBAY_LIBRARY_BUILDING)
set_target_properties(forebay_library PROPERTIES C_VISIBILITY_PRESET hidden)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(forebay_library PRIVATE -Wl,--exclude-libs,ALL)
endif()
spisor_strict_warnings(forebay_library)

# Each symbol is its definition's name with `_description`, the name weivel_describe's DESCRIBED_ELSEWHERE expects, so
# a library nesting one of forebay's definitions finds its description without being told the symbol.
file(GLOB forebay_described_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/forebay/*.h)
list(SORT forebay_described_headers)
weivel_describe(forebay_library
    HEADERS ${spisor_library_SOURCE_DIR}/include/spisor/status.h ${forebay_described_headers}
    PREFIX forebay_described_
    EXPORT FOREBAY_DEFINITION_API
    STATUS spisor_status
    REFUSAL spisor_refusal
    VALUE spisor_value
    TREE spisor_value_tree
    SERVICE forebay_routing_definition SYMBOL forebay_routing_definition_description
    SERVICE forebay_accounts_definition SYMBOL forebay_accounts_definition_description
    SERVICE forebay_anthropic_definition SYMBOL forebay_anthropic_definition_description)
target_link_libraries(forebay_library PRIVATE weivel_standard)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    enable_testing()
    include(${forebay_siblings}/library/cmake/test_timeouts.cmake)
    spisor_refuse_unbounded_tests("${CMAKE_CURRENT_SOURCE_DIR}")
    spisor_add_weivel()
    add_subdirectory(test)
endif()

spisor_refuse_test_only_links("${CMAKE_CURRENT_SOURCE_DIR}")
