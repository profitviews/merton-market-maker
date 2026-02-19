# Self-contained toolchain for building reflection-based Python modules
# inside the repo's Docker image.

set(CMAKE_CXX_COMPILER "/opt/clang-p2996/bin/clang++" CACHE FILEPATH "P2996 Clang")
set(REFLECT_PY_STRAT_CLANG_PREFIX "/opt/clang-p2996" CACHE PATH "Clang install prefix")
set(REFLECT_PY_STRAT_QL_INSTALL_DIR "/opt/ql_install" CACHE PATH "QuantLib install prefix")
set(REFLECT_PY_STRAT_LIBCXX "${REFLECT_PY_STRAT_CLANG_PREFIX}/lib/x86_64-unknown-linux-gnu/libc++.a" CACHE FILEPATH "Static libc++")
set(REFLECT_PY_STRAT_LIBCXXABI "${REFLECT_PY_STRAT_CLANG_PREFIX}/lib/x86_64-unknown-linux-gnu/libc++abi.a" CACHE FILEPATH "Static libc++abi")

set(REFLECT_PY_STRAT_CXX_FLAGS
  -O3 -fPIC
  -std=c++2c -freflection -freflection-latest -fexpansion-statements
  -stdlib=libc++ -nostdinc++
  -fvisibility=hidden
  -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
  -DQL_USE_STD_SHARED_PTR
  -ffunction-sections -fdata-sections
)

set(REFLECT_PY_STRAT_LINK_FLAGS
  -stdlib=libc++ -nostdlib++
  -Wl,--gc-sections -Wl,--as-needed
)

set(REFLECT_PY_STRAT_INCLUDE_DIRS
  "${REFLECT_PY_STRAT_QL_INSTALL_DIR}/include"
  "${REFLECT_PY_STRAT_CLANG_PREFIX}/include/c++/v1"
  "${REFLECT_PY_STRAT_CLANG_PREFIX}/include/x86_64-unknown-linux-gnu/c++/v1"
)
