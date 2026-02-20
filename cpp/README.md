# C++ Core For Real-Time Adaptive Merton Updates

This folder contains the heavy runtime path in C++ and a reflection-based pybind module stub.

## What Is Implemented

- `OnlineMertonCalibrator` in `include/merton_online_calibrator.hpp` / `src/merton_online_calibrator.cpp`
- Python extension stub in `src/python_module_entry.cpp` using `include/reflection_engine.hpp`
- Heavy computation path:
  - rolling return ingestion from ticks
  - Merton jump-diffusion negative log-likelihood over a rolling window
  - periodic online parameter improvement (`sigma`, `lambda`, `mu_j`, `delta_j`)
- Fair-value computation:
  - `E[S_T] = S_0 * exp((r - q - lambda * k) * T)`
  - `k = exp(mu_j + 0.5 * delta_j^2) - 1`

The heavy likelihood path uses an internal standard-normal density implementation
to keep the core portable in minimal build environments.

## Binding Surface

The module target `merton_online_calibrator.so` exposes:

- constructor: `OnlineMertonCalibrator(MertonParams initial, CalibratorConfig config={})`
- `bool update_tick(double price, int64_t epoch_us)`
- `bool maybe_update_params()`
- `double fair_value(double s0, double q_annual, double t_years, double r=0.0) const`
- `double fair_value_quantlib(double s0, double q_annual, double t_years, double r=0.0) const`
- `MertonParams params() const`
- `size_t sample_count() const`

All data members and public instance methods are bound through the
reflection engine in `include/reflection_engine.hpp` (no hand-written
per-member/per-method pybind mappings). Note that Python access to the
`lambda` field uses `getattr(obj, "lambda")` / `setattr(obj, "lambda", v)`
because `lambda` is a Python keyword.

Python usage pattern:

1. On each tick: `update_tick(price, ts_us)`
2. Every N returns: `maybe_update_params()`
3. For quote comparison: `fair_value(mid, q_annual, T_years, r)`
4. Periodically pull `params()` for logging / persistence

QuantLib integration (for illustration purposes):

- `fair_value_quantlib(...)` builds flat `r`/`q` curves with QuantLib
- computes forward from discount factors (`S0 * Dq / Dr`)
- applies the Merton jump compensator adjustment using current online parameters

## How `OnlineMertonCalibrator` Works

### 1) Tick ingestion (`update_tick`)

For each incoming `(price, epoch_us)`:

- validates price and timestamp ordering
- computes `log(price / last_price)`
- appends return and `dt_us` to rolling buffers
- trims buffers to `window_size`
- increments the update counter

This keeps per-tick work light while maintaining a fresh rolling sample.

### 2) Gated online recalibration (`maybe_update_params`)

Recalibration only runs when both are true:

- `sample_count >= min_points_for_update`
- enough new returns since last update (`update_every_n_returns`)

When triggered, it:

- estimates `dt` (years) from the median of observed `dt_us`
- evaluates current negative log-likelihood (NLL)
- runs a small coordinate-search around current parameters:
  - try plus/minus perturbations for each parameter
  - accept improvements greater than `improvement_tol`
  - shrink step sizes if no improvement
- clamps results to configured/safe bounds

This is a local, incremental update strategy designed for high-frequency runtime use.

### 3) Likelihood objective

The calibrator optimizes Merton jump-diffusion NLL over the rolling returns. The log-return PDF is:

$$
f(x \mid \sigma, \lambda, \mu_J, \delta_J, \Delta t)
  = \sum_{n=0}^{n_{\max}-1}
    \frac{e^{-\lambda \Delta t}(\lambda \Delta t)^n}{n!}
    \cdot \frac{\phi\bigl((x - \mu_n)/\sigma_n\bigr)}{\sigma_n}
$$

where $\phi$ is the standard normal density, $\kappa = e^{\mu_J + \delta_J^2/2} - 1$, and

$$
\mu_n = \left(-\lambda\kappa - \frac{\sigma^2}{2}\right)\Delta t + n\mu_J,
\quad
\sigma_n^2 = \sigma^2 \Delta t + n \delta_J^2
$$

The objective is:

$$
\text{NLL}(\theta) = -\sum_i \log f(x_i \mid \theta, \widehat{\Delta t})
$$

with $\widehat{\Delta t}$ taken as the median of observed inter-tick intervals (in years).

### 4) Fair value from current online parameters

At any point, fair value uses the current online parameters:

$$
\mathbb{E}[S_T \mid S_0] = S_0 \, \exp\bigl((r - q - \lambda\kappa)\,T\bigr),
\quad
\kappa = e^{\mu_J + \delta_J^2/2} - 1
$$

So the runtime loop is:

- `update_tick` (every tick)
- `maybe_update_params` (periodically)
- `fair_value` (as needed for signal/decision)

### Runtime pseudocode

```text
init calibrator(params0, config)

for each market tick (price, ts_us):
    accepted = calibrator.update_tick(price, ts_us)

    if accepted:
        updated = calibrator.maybe_update_params()
        if updated:
            params = calibrator.params()
            # optional: log/store params

    q_annual = funding_to_annual(funding_rate_8h)
    fair = calibrator.fair_value(price_or_mid, q_annual, T_years, r=0.0)
    diff = fair - market_price
    # publish signal / apply strategy logic
```

## Build

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

