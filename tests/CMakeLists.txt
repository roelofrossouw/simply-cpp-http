# Resources for the tests
set(RESOURCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resource")
set(RESOURCE_DESTINATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/resource")
add_custom_target(copy_http_resources ALL COMMAND ${CMAKE_COMMAND} -E copy_directory ${RESOURCE_SOURCE_DIR} ${RESOURCE_DESTINATION_DIR})

set(SC_TEST_LINK_LIBRARIES sc-http)

# rest runs against a stand-in server on this machine (python3 and openssl); ollama needs an Ollama
# server ("network": ctest -LE network leaves it out).
add_sc_test(rest LABELS unit TIMEOUT 120)
target_compile_definitions(test-rest PRIVATE SC_HTTP_TEST_RESOURCE="${CMAKE_CURRENT_SOURCE_DIR}/resource")
add_sc_test(ollama LABELS network TIMEOUT 600)
add_dependencies(test-ollama copy_http_resources)
