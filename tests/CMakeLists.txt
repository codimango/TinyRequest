# Test suite for TinyRequest core (non-UI) modules.
#
# Built when -DBUILD_TESTING=ON is passed to the top-level CMake (default OFF).
# Run via:  ctest --output-on-failure  (from the build directory)

# Unity is vendored under tests/unity/.
set(UNITY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/unity)

add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})
# Pretty output and fixture names in the runner.
target_compile_definitions(unity PUBLIC
    UNITY_INCLUDE_DOUBLE
    UNITY_OUTPUT_COLOR
)

# Common include dirs for the project under test.
set(TR_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${GLFW3_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)
if(CJSON_FOUND)
    list(APPEND TR_INCLUDES ${CJSON_INCLUDE_DIRS})
else()
    list(APPEND TR_INCLUDES ${CJSON_DIR})
endif()

# Helper: declare a Unity test binary.
#   tr_add_unit_test(<name> SOURCES <sources...> [LIBS <libs...>])
function(tr_add_unit_test name)
    cmake_parse_arguments(TR_T "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${TR_T_SOURCES})
    target_include_directories(${name} PRIVATE ${TR_INCLUDES})
    target_link_libraries(${name} PRIVATE unity ${TR_T_LIBS})
    if(UNIX AND NOT APPLE)
        target_link_libraries(${name} PRIVATE pthread)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # Each persistence test mutates $HOME; isolate per test.
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

# request_response: no external dependencies.
tr_add_unit_test(test_request_response
    SOURCES
        test_request_response.c
        ${CMAKE_SOURCE_DIR}/src/request_response.c
)

# collections + cookie jar: depends on request_response.
tr_add_unit_test(test_collections
    SOURCES
        test_collections.c
        ${CMAKE_SOURCE_DIR}/src/collections.c
        ${CMAKE_SOURCE_DIR}/src/request_response.c
)

tr_add_unit_test(test_cookie_jar
    SOURCES
        test_cookie_jar.c
        ${CMAKE_SOURCE_DIR}/src/collections.c
        ${CMAKE_SOURCE_DIR}/src/request_response.c
)

# http_client: needs libcurl headers + lib for the symbols it references.
tr_add_unit_test(test_http_client
    SOURCES
        test_http_client.c
        ${CMAKE_SOURCE_DIR}/src/http_client.c
        ${CMAKE_SOURCE_DIR}/src/collections.c
        ${CMAKE_SOURCE_DIR}/src/request_response.c
    LIBS
        ${CURL_LIBRARIES}
)

# persistence: needs cJSON + libcurl (via app_state.h chain).
set(PERSISTENCE_EXTRA_SOURCES "")
if(NOT CJSON_FOUND)
    list(APPEND PERSISTENCE_EXTRA_SOURCES ${CJSON_SOURCES})
endif()

tr_add_unit_test(test_persistence
    SOURCES
        test_persistence.c
        ${CMAKE_SOURCE_DIR}/src/persistence.c
        ${CMAKE_SOURCE_DIR}/src/collections.c
        ${CMAKE_SOURCE_DIR}/src/request_response.c
        ${PERSISTENCE_EXTRA_SOURCES}
    LIBS
        ${CURL_LIBRARIES}
)
if(CJSON_FOUND)
    target_link_libraries(test_persistence PRIVATE ${CJSON_LIBRARIES})
endif()
