cmake_minimum_required(VERSION 3.22)
set(SC_MODULE http)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-http" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_HTTP_BUILD_TESTS "Build simply-cpp-http tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

# sc::lowercase and file_get_contents (sc.h), nlohmann/json for sc::ollama; libcurl underneath sc::rest.
find_or_install_package(sc-core simply-cpp-dev simply-cpp 2.0.0)
find_or_install_package(CURL libcurl4-openssl-dev curl)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

add_sc_object(rest LINK_LIBRARIES sc::sc-core CURL::libcurl PUBLIC_LINK_LIBRARIES sc::sc-core CURL::libcurl)
add_sc_object(ollama LINK_LIBRARIES sc::sc-core nlohmann_json::nlohmann_json PUBLIC_LINK_LIBRARIES sc::sc-core)

add_sc_libraries(DESCRIPTION "simply-cpp HTTP: sc::rest over libcurl, and sc::ollama")
target_compile_features(sc-http PUBLIC cxx_std_20)
target_compile_features(sc-http-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-http
        COMPONENT development)

if (SC_HTTP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-http-test)
endif ()

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 2.0.0~)" libcurl4-openssl-dev)
