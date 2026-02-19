cmake_minimum_required(VERSION 3.20)
project(merton_core LANGUAGES CXX)

# Do not force a C++ standard here.
# The reflection toolchain flags provide the required language mode (e.g. -std=c++2c).

set(MERTON_CORE_SOURCES
    src/merton_online_calibrator.cpp
)

add_library(merton_core STATIC ${MERTON_CORE_SOURCES})
target_include_directories(merton_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# QuantLib: required for fair_value_quantlib helper.
if(DEFINED REFLECT_PY_STRAT_QL_INSTALL_DIR)
    set(JD_QL_ROOT "${REFLECT_PY_STRAT_QL_INSTALL_DIR}")
else()
    set(JD_QL_ROOT "/opt/ql_install")
endif()
find_path(JD_QUANTLIB_INCLUDE_DIR
    ql/quantlib.hpp
    PATHS
        "${JD_QL_ROOT}/include"
        /usr/include
        /usr/local/include
)
find_library(JD_QUANTLIB_LIBRARY
    NAMES QuantLib libQuantLib.a
    PATHS
        "${JD_QL_ROOT}/lib"
        "${JD_QL_ROOT}/lib64"
        /usr/lib
        /usr/local/lib
)
if(NOT JD_QUANTLIB_INCLUDE_DIR OR NOT JD_QUANTLIB_LIBRARY)
    message(FATAL_ERROR
        "QuantLib not found. Expected headers/libs under ${JD_QL_ROOT}.\n"
        "Set REFLECT_PY_STRAT_QL_INSTALL_DIR or install QuantLib dev libs.")
endif()
target_include_directories(merton_core PRIVATE "${JD_QUANTLIB_INCLUDE_DIR}")
target_link_libraries(merton_core PUBLIC "${JD_QUANTLIB_LIBRARY}")

# Python & pybind11 headers
# Prefer interpreter matching Docker ENV PYTHON_VERSION when available.
if(NOT Python3_EXECUTABLE AND DEFINED ENV{PYTHON_VERSION})
    find_program(_PYTHON_FROM_ENV NAMES "python$ENV{PYTHON_VERSION}")
    if(_PYTHON_FROM_ENV)
        set(Python3_EXECUTABLE "${_PYTHON_FROM_ENV}" CACHE FILEPATH "Preferred Python interpreter")
    endif()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# pybind11 discovery strategy:
# 1) CMake package (pybind11Config.cmake)
# 2) Python module include path (pybind11.get_include())
# 3) Common system include dirs (/usr/include, /usr/local/include)
set(PYBIND11_INCLUDE_DIR "")
find_package(pybind11 QUIET CONFIG)
if(pybind11_FOUND)
    get_target_property(_pybind11_inc pybind11::headers INTERFACE_INCLUDE_DIRECTORIES)
    if(_pybind11_inc)
        list(GET _pybind11_inc 0 PYBIND11_INCLUDE_DIR)
    endif()
endif()

if(NOT PYBIND11_INCLUDE_DIR)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import pybind11; print(pybind11.get_include())"
        OUTPUT_VARIABLE PYBIND11_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT PYBIND11_INCLUDE_DIR)
    find_path(PYBIND11_INCLUDE_DIR pybind11/pybind11.h
        PATHS /usr/include /usr/local/include
    )
endif()

if(NOT PYBIND11_INCLUDE_DIR)
    message(FATAL_ERROR
        "pybind11 headers not found.\n"
        "Install one of:\n"
        "  - Python module for active interpreter: ${Python3_EXECUTABLE} -m pip install pybind11\n"
        "  - System package (e.g. pybind11-dev)\n"
        "Or set PYBIND11_INCLUDE_DIR manually.")
endif()

# Python-loadable extension module: merton_online_calibrator.so
add_library(merton_online_calibrator MODULE
    src/python_module_entry.cpp
)
target_include_directories(merton_online_calibrator
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${Python3_INCLUDE_DIRS}
        ${PYBIND11_INCLUDE_DIR}
)
target_link_libraries(merton_online_calibrator
    PRIVATE
        merton_core
        Python3::Module
)

# Reflection defaults for portability when no external toolchain vars are provided.
if(NOT DEFINED REFLECT_PY_STRAT_CXX_FLAGS)
    set(REFLECT_PY_STRAT_CXX_FLAGS
        -O3 -fPIC
        -std=c++2c -freflection -freflection-latest -fexpansion-statements
        -stdlib=libc++ -nostdinc++
        -fvisibility=hidden
        -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
        -ffunction-sections -fdata-sections
    )
endif()
if(NOT DEFINED REFLECT_PY_STRAT_LINK_FLAGS)
    set(REFLECT_PY_STRAT_LINK_FLAGS
        -stdlib=libc++ -nostdlib++
        -Wl,--gc-sections -Wl,--as-needed
    )
endif()
if(NOT DEFINED REFLECT_PY_STRAT_INCLUDE_DIRS)
    set(REFLECT_PY_STRAT_INCLUDE_DIRS
        /opt/clang-p2996/include/c++/v1
        /opt/clang-p2996/include/x86_64-unknown-linux-gnu/c++/v1
    )
endif()

target_compile_options(merton_core PRIVATE ${REFLECT_PY_STRAT_CXX_FLAGS})
target_compile_options(merton_online_calibrator PRIVATE ${REFLECT_PY_STRAT_CXX_FLAGS})
target_link_options(merton_online_calibrator PRIVATE ${REFLECT_PY_STRAT_LINK_FLAGS})
target_include_directories(merton_core PRIVATE ${REFLECT_PY_STRAT_INCLUDE_DIRS})
target_include_directories(merton_online_calibrator PRIVATE ${REFLECT_PY_STRAT_INCLUDE_DIRS})

# Explicit runtime linkage for portable extension loading.
# Avoids unresolved C++ ABI symbols at import time.
target_link_libraries(merton_online_calibrator PRIVATE
    m c dl pthread gcc_s
)
if(DEFINED REFLECT_PY_STRAT_LIBCXX AND EXISTS "${REFLECT_PY_STRAT_LIBCXX}")
    target_link_libraries(merton_online_calibrator PRIVATE "${REFLECT_PY_STRAT_LIBCXX}")
endif()
if(DEFINED REFLECT_PY_STRAT_LIBCXXABI AND EXISTS "${REFLECT_PY_STRAT_LIBCXXABI}")
    target_link_libraries(merton_online_calibrator PRIVATE "${REFLECT_PY_STRAT_LIBCXXABI}")
endif()

set_target_properties(merton_online_calibrator PROPERTIES
    PREFIX ""
    OUTPUT_NAME "merton_online_calibrator"
    SUFFIX ".so"
)

