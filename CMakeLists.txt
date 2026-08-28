cmake_minimum_required(VERSION 3.7)
project(register_student VERSION 0.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    # Linux GCC options
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC")
endif()

# OpenSSL for HTTPS support
find_package(OpenSSL REQUIRED)

# Include paths
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)
include_directories(SYSTEM
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd/include
    ${CMAKE_SOURCE_DIR}/3rd/include/sqlite3
    ${CMAKE_SOURCE_DIR}/3rd/include/inih
    ${CMAKE_SOURCE_DIR}/3rd/include/minizip
    ${OPENSSL_INCLUDE_DIR}
)

# Source files (using GLOB_RECURSE for automatic discovery)
file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp")
# Exclude shell.c (SQLite CLI tool source, not needed by vStuSystem)
file(GLOB_RECURSE THIRD_PARTY_SOURCES "3rd/src/*.c" "3rd/src/*.cpp")
list(FILTER THIRD_PARTY_SOURCES EXCLUDE REGEX "3rd/src/sqlite3/shell\\.c$")

set(SOURCES
    ${PROJECT_SOURCES}
    ${THIRD_PARTY_SOURCES}
)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Link libraries
link_directories(${CMAKE_SOURCE_DIR}/3rd/libs/x86_64/sqlite3)

add_executable(vStuSystem ${SOURCES})

# Test executable
file(GLOB TEST_PROJECT_SOURCES "tests/*.cpp")

# Exclude main.cpp from test sources (has its own main())
list(FILTER PROJECT_SOURCES EXCLUDE REGEX "src/main\\.cpp$")

set(TEST_SOURCES
    ${TEST_PROJECT_SOURCES}
    ${PROJECT_SOURCES}
    ${THIRD_PARTY_SOURCES}
)

add_executable(run_tests ${TEST_SOURCES})

if(WIN32)
    # MSVC /MT static runtime (CMake 3.7 compatible)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MT")

    # Treat source files as UTF-8 (Chinese string literals in Linux-era code)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

    # Disable MSVC implicit narrowing conversion warnings (C4244: int64_t->int,
    # C4267: size_t->unsigned int) — all conversions are safe in this project
    # (row counts, IDs, array indices never exceed target range)
    # Also disable C4996 (MSVC "unsafe" deprecation for _wfopen/inet_addr etc.)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244 /wd4267 /wd4996")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /wd4267 /wd4996")

    # Windowed subsystem: no console window on launch
    set_target_properties(vStuSystem PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS /MANIFEST:NO")
    set_target_properties(run_tests PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")

    # SQLite3 amalgamation compile macros + Winsock/Asio compatibility
    target_compile_definitions(vStuSystem PRIVATE
        SQLITE_OS_WIN
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION
        WIN32_LEAN_AND_MEAN
        ASIO_STANDALONE
        NOMINMAX
        BOOST_ALL_NO_LIB
        BOOST_DATE_TIME_NO_LIB
        BOOST_FILESYSTEM_NO_LIB
        BOOST_SYSTEM_NO_LIB
        CROW_USE_SSL
    )
    target_compile_definitions(run_tests PRIVATE
        SQLITE_OS_WIN
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION
        WIN32_LEAN_AND_MEAN
        ASIO_STANDALONE
        NOMINMAX
        BOOST_ALL_NO_LIB
        BOOST_DATE_TIME_NO_LIB
        BOOST_FILESYSTEM_NO_LIB
        BOOST_SYSTEM_NO_LIB
        CROW_USE_SSL
    )

    # Silence MSVC warnings on sqlite3.c (single-file 9MB)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/3rd/src/sqlite3/sqlite3.c
        PROPERTIES COMPILE_FLAGS "/W0 /bigobj"
    )

    # Embed PE resources (icon, manifest) into vStuSystem.exe
    target_sources(vStuSystem PRIVATE ${CMAKE_SOURCE_DIR}/vStuSystem.rc)

    # Copy OpenSSL DLLs to output directory for packaging
    add_custom_command(TARGET vStuSystem POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DTARGET_EXE="$<TARGET_FILE:vStuSystem>"
            -DOPENSSL_LIB="$<TARGET_FILE:OpenSSL::SSL>"
            -DOUTPUT_DIR="${CMAKE_SOURCE_DIR}/bin"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_openssl_dlls.cmake"
    )

    # Windows system libraries
    target_link_libraries(vStuSystem ws2_32 advapi32 shell32 user32 gdi32 iphlpapi OpenSSL::SSL OpenSSL::Crypto)
    target_link_libraries(run_tests ws2_32 advapi32 shell32 user32 gdi32 iphlpapi OpenSSL::SSL OpenSSL::Crypto)

    # Windows-only test executable for win-build-pack tests
    # win_pack/test_main.cpp #includes the other .cpp files (single-TU pattern)
    set(WIN_PACK_TEST_SOURCES
        ${CMAKE_SOURCE_DIR}/tests/win_pack/test_main.cpp
    )
    if(WIN_PACK_TEST_SOURCES)
        # Exclude main.cpp from win_pack test sources
        set(WIN_PACK_TEST_EXCLUDE_MAIN ${PROJECT_SOURCES})
        add_executable(run_win_pack_tests
            ${WIN_PACK_TEST_SOURCES}
            ${WIN_PACK_TEST_EXCLUDE_MAIN}
            ${THIRD_PARTY_SOURCES}
        )
        target_compile_definitions(run_win_pack_tests PRIVATE
            SQLITE_OS_WIN
            SQLITE_THREADSAFE=1
            SQLITE_OMIT_LOAD_EXTENSION
            WIN32_LEAN_AND_MEAN
            ASIO_STANDALONE
            NOMINMAX
            BOOST_ALL_NO_LIB
            BOOST_DATE_TIME_NO_LIB
            BOOST_FILESYSTEM_NO_LIB
            BOOST_SYSTEM_NO_LIB
            CROW_USE_SSL
        )
        set_target_properties(run_win_pack_tests PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
        target_link_libraries(run_win_pack_tests ws2_32 advapi32 shell32 user32 gdi32 iphlpapi OpenSSL::SSL OpenSSL::Crypto)
    endif()
else()
    # Linux branch
    target_compile_definitions(vStuSystem PRIVATE CROW_USE_SSL)
    target_compile_definitions(run_tests PRIVATE CROW_USE_SSL)
    target_link_libraries(vStuSystem sqlite3 pthread OpenSSL::SSL OpenSSL::Crypto)
    target_link_libraries(run_tests sqlite3 pthread OpenSSL::SSL OpenSSL::Crypto)

    # Silence GCC warnings in sqlite3.c amalgamation (single-file ~9MB)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/3rd/src/sqlite3/sqlite3.c
        PROPERTIES COMPILE_FLAGS "-w"
    )
endif()
