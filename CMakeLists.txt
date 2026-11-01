cmake_minimum_required(VERSION 3.20)

project(SC4QuantumNet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(QUANTUMNET_BUILD_TESTS "Build the platform-independent simulation tests" ON)

# ---------------------------------------------------------------------
# Platform-independent simulation core (also used by the tests)
# ---------------------------------------------------------------------
add_library(quantumnet_core STATIC
    src/QuantumNetSimulation.cpp
    src/QuantumNetConfig.cpp
)
target_include_directories(quantumnet_core PUBLIC src)

if (MSVC)
    target_compile_options(quantumnet_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(quantumnet_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------
# SimCity 4 DLL (Windows, 32-bit only: SC4 is a 32-bit game)
# ---------------------------------------------------------------------
if (WIN32)
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "SimCity 4 is a 32-bit game: configure with -A Win32.")
    endif()

    set(GZCOM_DIR ${CMAKE_CURRENT_LIST_DIR}/vendor/gzcom-dll/gzcom-dll)
    if (NOT EXISTS ${GZCOM_DIR}/include/cRZCOMDllDirector.h)
        message(FATAL_ERROR "gzcom-dll SDK missing: run 'git submodule update --init'.")
    endif()

    add_library(SC4QuantumNet SHARED
        src/QuantumNetDllDirector.cpp
        src/RegionalSupplyLuaBridge.cpp
        src/Logger.cpp
        src/version.rc
        # Only the SDK sources this plugin needs (the SDK also ships example
        # directors that would clash with ours).
        ${GZCOM_DIR}/src/cRZCOMDllDirector.cpp
        ${GZCOM_DIR}/src/cRZMessage2.cpp
        ${GZCOM_DIR}/src/cRZMessage2Standard.cpp
        ${GZCOM_DIR}/src/cRZBaseString.cpp
        ${GZCOM_DIR}/src/SC4String.cpp
        ${GZCOM_DIR}/src/STLPORTCharString.cpp
        ${GZCOM_DIR}/src/SCLuaUtil.cpp
    )
    target_include_directories(SC4QuantumNet PRIVATE src ${GZCOM_DIR}/include)
    target_link_libraries(SC4QuantumNet PRIVATE quantumnet_core)
    target_compile_definitions(SC4QuantumNet PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

    if (MSVC)
        # Static CRT: users do not need the VC++ redistributable.
        set_property(TARGET SC4QuantumNet quantumnet_core PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(SC4QuantumNet PRIVATE /W3 /permissive-)
    else()
        target_link_options(SC4QuantumNet PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    set_target_properties(SC4QuantumNet PROPERTIES PREFIX "" OUTPUT_NAME "SC4QuantumNet")
endif()

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
if (QUANTUMNET_BUILD_TESTS)
    enable_testing()
    add_executable(quantumnet_tests tests/QuantumNetTests.cpp)
    target_link_libraries(quantumnet_tests PRIVATE quantumnet_core)
    target_compile_definitions(quantumnet_tests PRIVATE
        QUANTUMNET_SAMPLE_INI="${CMAKE_CURRENT_LIST_DIR}/config/SC4QuantumNet.ini")
    if (MSVC)
        set_property(TARGET quantumnet_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME quantumnet_tests COMMAND quantumnet_tests)
endif()
