foreach(name list_test descriptor_test text_test home_test current_test call_test)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE forebay_library spisor_library)
    spisor_strict_warnings(${name})
    target_compile_definitions(${name} PRIVATE APPS_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

add_executable(routing_pattern_test routing_pattern_test.c ${PROJECT_SOURCE_DIR}/src/routing_pattern.c)
target_include_directories(routing_pattern_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
spisor_strict_warnings(routing_pattern_test)
add_test(NAME routing_pattern_test COMMAND routing_pattern_test)

file(GLOB routing_vectors CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vectors/routing/*.json)
list(SORT routing_vectors)
string(REPLACE ";" "
" routing_vector_lines "${routing_vectors}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/routing_vectors.txt "${routing_vector_lines}
")
add_executable(routing_test routing_test.c)
target_link_libraries(routing_test PRIVATE forebay_library spisor_library)
spisor_strict_warnings(routing_test)
target_compile_definitions(routing_test PRIVATE
    APPS_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}"
    ROUTING_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/routing_vectors.txt")
add_test(NAME routing_test COMMAND routing_test)

add_test(NAME sources_are_ascii
    COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/src -P ${spisor_library_SOURCE_DIR}/test/ascii_check.cmake)

file(GLOB accounts_vectors CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vectors/accounts/*.json)
list(SORT accounts_vectors)
string(REPLACE ";" "
" accounts_vector_lines "${accounts_vectors}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/accounts_vectors.txt "${accounts_vector_lines}
")
add_executable(accounts_test accounts_test.c)
target_link_libraries(accounts_test PRIVATE forebay_library spisor_library)
spisor_strict_warnings(accounts_test)
target_compile_definitions(accounts_test PRIVATE
    APPS_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}"
    ACCOUNTS_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/accounts_vectors.txt")
add_test(NAME accounts_test COMMAND accounts_test)

add_executable(accounts_concurrent_test accounts_concurrent_test.c)
target_link_libraries(accounts_concurrent_test PRIVATE forebay_library spisor_library)
spisor_strict_warnings(accounts_concurrent_test)
target_compile_definitions(accounts_concurrent_test PRIVATE APPS_SCRATCH_PATH="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME accounts_concurrent_test COMMAND accounts_concurrent_test)
