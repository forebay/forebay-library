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

add_executable(routing_pattern_test routing_pattern_test.c ${PROJECT_SOURCE_DIR}/src/routing_pattern.c)
target_include_directories(routing_pattern_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(routing_pattern_test PRIVATE spisor_library spisor_allocator)
spisor_strict_warnings(routing_pattern_test)
add_test(NAME routing_pattern_test COMMAND routing_pattern_test)

forebay_vector_list(routing)
set(forebay_routing_vectors ROUTING_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/routing_vectors.txt")
forebay_definition_test(routing_test spisor_allocator ${forebay_routing_vectors})
forebay_definition_test(routing_out_of_memory_test spisor_allocator_counting ${forebay_routing_vectors})
forebay_definition_test(routing_step_test spisor_allocator)

add_test(NAME sources_are_ascii
    COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/src
        -P ${forebay_spisor_test_directory}/ascii_check.cmake)
add_test(NAME public_headers_are_ascii
    COMMAND ${CMAKE_COMMAND} -DDIRECTORY=${PROJECT_SOURCE_DIR}/include
        -P ${forebay_spisor_test_directory}/ascii_check.cmake)

include(${spisor_library_SOURCE_DIR}/cmake/header_isolation.cmake)
file(GLOB forebay_isolated_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/forebay/*.h)
spisor_refuse_name_in_headers(public_headers_without_cjson NAME cJSON HEADERS ${forebay_isolated_headers}
    INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/include ${spisor_library_SOURCE_DIR}/include)

forebay_vector_list(accounts)
set(forebay_accounts_vectors ACCOUNTS_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/accounts_vectors.txt")
forebay_definition_test(accounts_test spisor_allocator ${forebay_accounts_vectors})
forebay_definition_test(accounts_out_of_memory_test spisor_allocator_counting ${forebay_accounts_vectors})

# IR JSON to forebay/ir.h's structs and back, for tests that build IR inputs and compare IR outputs.
add_library(forebay_ir_json STATIC EXCLUDE_FROM_ALL
    ir_json_arena.c
    ir_json_names.c
    ir_json_read.c
    ir_json_read_blocks.c
    ir_json_read_events.c
    ir_json_read_members.c
    ir_json_schema_form.c
    ir_json_write.c
    ir_json_write_blocks.c
    ir_json_write_events.c
    ir_json_write_members.c)
target_include_directories(forebay_ir_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(forebay_ir_json PUBLIC spisor_library)
set_target_properties(forebay_ir_json PROPERTIES SPISOR_TEST_ONLY TRUE)
spisor_strict_warnings(forebay_ir_json)

forebay_vector_list(ir)
forebay_vector_list(anthropic)
forebay_definition_test(ir_codec_test spisor_allocator
    IR_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/ir_vectors.txt"
    ANTHROPIC_VECTOR_LIST="${CMAKE_CURRENT_BINARY_DIR}/anthropic_vectors.txt")
target_link_libraries(ir_codec_test PRIVATE forebay_ir_json)

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

file(GLOB forebay_public_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/forebay/*.h)
set(forebay_definition_symbols "")
set(forebay_definition_header_count 0)
foreach(header IN LISTS forebay_public_headers)
    file(READ ${header} header_text)
    if(header_text MATCHES "#include \"forebay/definition.h\"")
        math(EXPR forebay_definition_header_count "${forebay_definition_header_count} + 1")
    endif()
    string(REGEX MATCHALL "forebay_[a-z_]+_definition_of[ \t\r\n]*[(][ \t\r\n]*void[ \t\r\n]*[)]"
        declarations "${header_text}")
    foreach(declaration IN LISTS declarations)
        string(REGEX MATCH "forebay_[a-z_]+_definition_of" symbol "${declaration}")
        list(APPEND forebay_definition_symbols ${symbol})
    endforeach()
endforeach()
list(REMOVE_DUPLICATES forebay_definition_symbols)
string(JOIN [[", "]] forebay_definition_symbol_list ${forebay_definition_symbols})
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/definition_symbols.h
    CONTENT [[#define FOREBAY_DEFINITION_SYMBOLS "@forebay_definition_symbol_list@"
#define FOREBAY_DEFINITION_HEADER_COUNT @forebay_definition_header_count@
]])

add_executable(declaration_test declaration_test.c)
target_include_directories(declaration_test PRIVATE ${forebay_spisor_test_directory}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(declaration_test PRIVATE FOREBAY_DECLARATION_PATH="${PROJECT_SOURCE_DIR}/library.json")
target_link_libraries(declaration_test PRIVATE weivel_standard)
spisor_strict_warnings(declaration_test)
add_test(NAME declaration_test COMMAND declaration_test)

add_executable(weivel_host_load_test weivel_host_load_test.c)
target_include_directories(weivel_host_load_test PRIVATE ${forebay_spisor_test_directory}
    ${PROJECT_SOURCE_DIR}/include ${spisor_library_SOURCE_DIR}/include)
target_compile_definitions(weivel_host_load_test PRIVATE
    FOREBAY_DECLARATION_PATH="${PROJECT_SOURCE_DIR}/library.json"
    FOREBAY_LIBRARY_PATH="$<TARGET_FILE:forebay_library>" WEIVEL_CORE_PATH="$<TARGET_FILE:weivel_core_shared>")
target_link_libraries(weivel_host_load_test PRIVATE weivel_c weivel_standard)
add_dependencies(weivel_host_load_test forebay_library weivel_core_shared)
spisor_strict_warnings(weivel_host_load_test)
add_test(NAME weivel_host_load_test COMMAND weivel_host_load_test)

if(MSVC)
    set(forebay_export_lister ${CMAKE_LINKER})
    set(forebay_export_format pe)
elseif(APPLE)
    set(forebay_export_lister ${CMAKE_NM})
    set(forebay_export_format macho)
else()
    set(forebay_export_lister ${CMAKE_NM})
    set(forebay_export_format elf)
endif()
string(JOIN "," forebay_expected_exports ${forebay_definition_symbols})
add_test(NAME forebay_exports_only_definitions_test
    COMMAND ${CMAKE_COMMAND} -DLIBRARY=$<TARGET_FILE:forebay_library> -DLISTER=${forebay_export_lister}
        -DFORMAT=${forebay_export_format} -DEXPECTED=${forebay_expected_exports}
        -P ${spisor_library_SOURCE_DIR}/cmake/exports_check.cmake)

include(${spisor_library_SOURCE_DIR}/cmake/definition_calls.cmake)
set(forebay_definition_operations ${CMAKE_CURRENT_BINARY_DIR}/generated/definition_operations.c)
spisor_definition_operations(${forebay_definition_operations} DEFINITIONS ${PROJECT_SOURCE_DIR}/include/forebay
    INCLUDE_ROOT ${PROJECT_SOURCE_DIR}/include PREFIX forebay_ LAYOUTS layouts SHAPES ${PROJECT_SOURCE_DIR}/include)
# Both tests compile the generated table, so it is generated once ahead of them rather than by each in a parallel build.
add_custom_target(forebay_definition_operations DEPENDS ${forebay_definition_operations})
foreach(name IN ITEMS short_shape_test empty_answer_test)
    spisor_add_definition_call_test(${name} SOURCE ${name}.c LIBRARY forebay_library
        OPERATIONS ${forebay_definition_operations})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_dependencies(${name} forebay_definition_operations)
endforeach()
