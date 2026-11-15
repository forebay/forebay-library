add_executable(routing_benchmark routing_benchmark.c)
target_link_libraries(routing_benchmark PRIVATE forebay_library spisor_library)
spisor_strict_warnings(routing_benchmark)
add_test(NAME routing_definition_spike COMMAND routing_benchmark)

# The Test workflow builds Debug and prints only failing tests, so the timing runs in a Release tree of its own here.
if(NOT SPIKE_NESTED_RELEASE)
    add_custom_target(spike_release_benchmark ALL
        COMMAND ${CMAKE_COMMAND} -DSOURCE=${PROJECT_SOURCE_DIR} -DBINARY=${CMAKE_BINARY_DIR}/spike-release
                -DGENERATOR=${CMAKE_GENERATOR} -DTARGET=routing_benchmark -P ${CMAKE_CURRENT_SOURCE_DIR}/release_benchmark.cmake
        DEPENDS routing_benchmark
        VERBATIM)
endif()
