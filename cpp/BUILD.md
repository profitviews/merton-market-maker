# Building the System

This repo includes `cpp/CMakeLists.txt` for:

- static core library `merton_core`
- Python extension module `merton_online_calibrator` (`.so` or platform-tagged name depending on backend)

## Choose a binding backend

Set `MERTON_PYTHON_BINDING` to either:

- `pybind11` (default)
- `nanobind`

### CMake examples

`pybind11`:

```bash
cmake -S cpp -B cpp/build -DMERTON_PYTHON_BINDING=pybind11
cmake --build cpp/build -j
```

`nanobind`:

```bash
cmake -S cpp -B cpp/build -DMERTON_PYTHON_BINDING=nanobind
cmake --build cpp/build -j
```

### `just` examples

`pybind11`:

```bash
cd cpp
MERTON_PYTHON_BINDING=pybind11 just test
```

`nanobind`:

```bash
cd cpp
MERTON_PYTHON_BINDING=nanobind just test
```

The reflection-based binding logic is shared conceptually but implemented in parallel headers under `include/reflection_bind_*.hpp`.

With `nanobind`, the extension filename follows Python's platform tag (for example `merton_online_calibrator.cpython-39-x86_64-linux-gnu.so`). Keep `build/` on `PYTHONPATH` as usual; `import merton_online_calibrator` works the same.

## Self-contained Docker build (recommended)

```bash
cd cpp
just test
```

This will:

- build the local `cpp/Dockerfile` image
- configure using local `cpp/toolchain-p2996.cmake`
- produce `cpp/build/merton_online_calibrator` (`.so` with `pybind11`, or Python's tagged suffix with `nanobind`)
- run the `pytest` suite in `tests/` inside the container with Python 3.9

Override Python version if needed:

```bash
PY_VER=3.9 just test
```

Override backend or Docker networking if needed:

```bash
MERTON_PYTHON_BINDING=nanobind just test
DOCKER_NETWORK=bridge just docker-build
```

## Ad-hoc compiler use

The p2996 Clang compiler (C++26, reflection) is available for compiling small test files or experiments. Build the image first with `just test` or `just build-host`.

**Interactive shell** - drop into bash with `clang++` and QuantLib on `PATH`:

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
