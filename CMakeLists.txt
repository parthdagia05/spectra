cmake_minimum_required(VERSION 3.16)
project(spectra C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(CJSON REQUIRED libcjson)

set(SPECTRA_SOURCES
    src/main.c
    src/event.c
    src/correlator.c
    src/report.c
    src/process.c
    src/store.c
    src/collector_sensor.c
    src/collector_net.c
    src/platform/android.c
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPECTRA_SOURCES src/platform/linux.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND SPECTRA_SOURCES src/platform/darwin.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND SPECTRA_SOURCES src/platform/windows.c)
endif()

add_executable(spectra ${SPECTRA_SOURCES})

target_include_directories(spectra PRIVATE
    ${SQLITE3_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_directories(spectra PRIVATE
    ${SQLITE3_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

target_link_libraries(spectra
    ${SQLITE3_LIBRARIES}
    ${CJSON_LIBRARIES}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(spectra mnl)
endif()

# Tests
add_executable(test_correlator
    test/test_correlator.c
    src/correlator.c
)
target_include_directories(test_correlator PRIVATE
    ${CJSON_INCLUDE_DIRS}
)
enable_testing()
add_test(NAME correlator COMMAND test_correlator)
