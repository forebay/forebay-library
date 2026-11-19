spisor_bound_tests()

set(forebay_spisor_test_directory ${spisor_library_SOURCE_DIR}/test)

function(forebay_vector_list service)
    file(GLOB vectors CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vectors/${service}/*.json)
    list(SORT vectors)
    string(REPLACE ";" "\n" vector_lines "${vectors}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${service}_vectors.txt "${vector_lines}\n")
endfunction()

# A test calling the definitions through the library's objects, so it can choose the allocator it counts with.
function(forebay_definition_test name allocator)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE forebay_objects ${allocator} spisor_vector_reader)
    target_include_directories(${name} PRIVATE ${spisor_library_SOURCE_DIR}/src ${forebay_spisor_test_directory})
    target_compile_definitions(${name} PRIVATE LIBRARY_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}" ${ARGN})
    spisor_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_executable(release_test release_test.c)
target_link_libraries(release_test PRIVATE forebay_library)
spisor_strict_warnings(release_test)
add_test(NAME release_test COMMAND release_test)

add_executable(routing_pattern_test routing_pattern_test.c ${PROJECT_SOURCE_DIR}/src/routing_pattern.c)
target_include_directories(routing_pattern_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(routing_pattern_test PRIVATE spisor_library spisor_allocator)
spisor_strict_warnings(routing_pattern_test)
add_test(NAME routing_pattern_test COMMAND routing_pattern_test)

forebay_vector_list(routing)
add_executable(routing_test routing_test.c)
target_link_libraries(routing_test PRIVATE forebay_library spisor_library spisor_allocator)
spisor_strict_warnings(routing_test)
target_compile_definitions(routing_test PRIVATE
    LIBRARY_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}"
    ROUTING_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/routing_vectors.txt")
add_test(NAME routing_test COMMAND routing_test)

add_executable(routing_step_test routing_step_test.c)
target_link_libraries(routing_step_test PRIVATE forebay_library spisor_library spisor_allocator)
spisor_strict_warnings(routing_step_test)
target_compile_definitions(routing_step_test PRIVATE LIBRARY_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME routing_step_test COMMAND routing_step_test)

add_test(NAME sources_are_ascii
    COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/src
        -P ${forebay_spisor_test_directory}/ascii_check.cmake)
add_test(NAME public_headers_are_ascii
    COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/include
        -P ${forebay_spisor_test_directory}/ascii_check.cmake)

forebay_vector_list(accounts)
set(forebay_accounts_vectors ACCOUNTS_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/accounts_vectors.txt")
forebay_definition_test(accounts_test spisor_allocator ${forebay_accounts_vectors})
forebay_definition_test(accounts_out_of_memory_test spisor_allocator_counting ${forebay_accounts_vectors})

add_executable(accounts_concurrent_test accounts_concurrent_test.c)
target_link_libraries(accounts_concurrent_test PRIVATE forebay_library spisor_library spisor_allocator)
target_include_directories(accounts_concurrent_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
spisor_strict_warnings(accounts_concurrent_test)
target_compile_definitions(accounts_concurrent_test PRIVATE LIBRARY_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME accounts_concurrent_test COMMAND accounts_concurrent_test)

add_executable(layouts layouts.c)
target_include_directories(layouts PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(layouts PRIVATE spisor_library)
spisor_strict_warnings(layouts)
add_test(NAME layouts_list_every_public_struct_and_field
    COMMAND ${CMAKE_COMMAND} -DLAYOUTS=$<TARGET_FILE:layouts> -DHEADERS=${PROJECT_SOURCE_DIR}/include
        "-DPREFIX=^forebay_" -P ${spisor_library_SOURCE_DIR}/cmake/layouts_check.cmake)
