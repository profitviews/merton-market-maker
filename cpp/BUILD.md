# Building the System

This repo includes `cpp/CMakeLists.txt` for:

- static core library `merton_core`
- Python extension module `merton_online_calibrator.so`

Example:

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build -j
```

### Self-contained Docker build (recommended)

```bash
cd cpp
just test
```

This will:

- build the local `cpp/Dockerfile` image
- configure using local `cpp/toolchain-p2996.cmake`
- produce `cpp/build/merton_online_calibrator.so`
- run the `pytest` suite in `tests/` inside the container with Python 3.9

Override Python version if needed:

```bash
PY_VER=3.9 just test
```

### Ad-hoc compiler use

The p2996 Clang compiler (C++26, reflection) is available for compiling small test files or experiments. Build the image first with `just test` or `just build-host`.

**Interactive shell** — drop into bash with `clang++` and QuantLib on `PATH`:

```bash
just shell
```

Inside the shell (repo mounted at `/workspace`):

```bash
clang++ -std=c++2c -freflection -freflection-latest -fexpansion-statements \
  -stdlib=libc++ -nostdinc++ -O3 \
  -I/opt/clang-p2996/include/c++/v1 \
  -I/opt/clang-p2996/include/x86_64-unknown-linux-gnu/c++/v1 \
  -o prog prog.cpp
```

**From outside the container:**

```bash
just clang-pp -- -std=c++2c -freflection -freflection-latest -fexpansion-statements \
  -stdlib=libc++ -nostdinc++ -O3 \
  -I/opt/clang-p2996/include/c++/v1 \
  -I/opt/clang-p2996/include/x86_64-unknown-linux-gnu/c++/v1 \
  -o prog prog.cpp
```

Or use the `merton-clang++` script (add `cpp/` to `PATH` or run from `cpp/`):

```bash
merton-clang++ -std=c++2c -freflection -freflection-latest -fexpansion-statements \
  -stdlib=libc++ -nostdinc++ -O3 \
  -I/opt/clang-p2996/include/c++/v1 \
  -I/opt/clang-p2996/include/x86_64-unknown-linux-gnu/c++/v1 \
  -o prog prog.cpp
```

For QuantLib, add `-I/opt/ql_install/include -L/opt/ql_install/lib -lQuantLib` and `-DQL_USE_STD_SHARED_PTR` when linking.

Requirements:
- `just`
- Docker
Everything else will be pulled into the container.
