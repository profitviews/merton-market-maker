# Merton Market Maker (BitMEX XBTUSDT)

This repository implements an end-to-end Merton jump-diffusion workflow for:

- offline calibration from historical data
- real-time theoretical price computation
- optional C++ acceleration for online/adaptive parameter updates

Only private local path config is stored in `.env` (via `python-dotenv`), so
machine-specific offline data locations stay out of source control.

## What This Repo Contains

- `jump_diff.ipynb`  
  Notebook for data loading, return construction, MLE calibration, and fit visualization.
- `profitview_merton_signal.py`  
  ProfitView strategy script that consumes live market updates and computes theoretical value.
- `cpp/`  
  C++ core (`OnlineMertonCalibrator`) + reflection-based Python extension module build.

## Model Summary

The core expectation used for fair value is:

- `E[S_T] = S_0 * exp((r - q - lambda * k) * T)`
- `k = exp(mu_j + 0.5 * delta_j^2) - 1`

Where:

- `sigma, lambda, mu_j, delta_j` come from calibration
- `q` is funding-rate carry (annualized)
- `T` is horizon (commonly the next 8h funding window)

The jump-diffusion process assumed is:

`dS_t / S_t = (r - q - lambda * k) dt + sigma dW_t + (J - 1) dN_t`

with `k = E[J - 1] = exp(mu_j + 0.5 * delta_j^2) - 1`.

## Mathematical Framework

### Process specification

The asset price $S_t$ follows a Merton (1976) jump-diffusion:

$$
\frac{dS_t}{S_t} = (\mu - \lambda \kappa)\,dt + \sigma\,dW_t + (J - 1)\,dN_t
$$

where:

- $W_t$ is a standard Brownian motion
- $N_t$ is a Poisson process with intensity $\lambda$ (jumps per year)
- $J$ is the multiplicative jump size; $\log J \sim \mathcal{N}(\mu_J, \delta_J^2)$

The jump compensator $\kappa = \mathbb{E}[J - 1]$ removes jump drift from the instantaneous return:

$$
\kappa = e^{\mu_J + \frac{1}{2}\delta_J^2} - 1
$$

Under the risk-neutral measure (or for fair-value pricing), the drift is $r - q - \lambda\kappa$, where $r$ is the risk-free rate and $q$ is the carry (e.g. funding rate annualized).

### Fair value

Under the model, the forward expectation is:

$$
\mathbb{E}[S_T \mid S_0] = S_0 \, \exp\bigl((r - q - \lambda\kappa)\,T\bigr)
$$

### Log-return distribution

Let $x_t = \log(S_t / S_{t-\Delta t})$ denote the log return over $\Delta t$. The PDF is a Poisson-weighted Gaussian mixture:

$$
f(x \mid \sigma, \lambda, \mu_J, \delta_J, \Delta t)
  = \sum_{n=0}^{n_{\max}} \frac{e^{-\lambda \Delta t}(\lambda \Delta t)^n}{n!}
    \cdot \phi\left(\frac{x - \mu_n}{\sigma_n}\right) \frac{1}{\sigma_n}
$$

where $\phi$ is the standard normal density and, conditioning on $n$ jumps:

$$
\mu_n = \left(-\lambda\kappa - \frac{\sigma^2}{2}\right)\Delta t + n\mu_J,
\quad
\sigma_n^2 = \sigma^2 \Delta t + n \delta_J^2
$$

### Calibration objective

We fit parameters by maximum likelihood over observed returns $\{x_i\}$:

$$
\hat\theta = \arg\min_\theta \;
  \mathcal{L}(\theta) = -\sum_i \log f(x_i \mid \theta, \Delta t)
$$

with $\theta = (\sigma, \lambda, \mu_J, \delta_J)$ and $\theta$ constrained to plausible ranges.

## Conceptual Architecture

### Phase A: Offline Calibration (Python)

- Data: historical bars or resampled trade data
- Feature: log-returns `x_t = ln(P_t / P_(t-1))`
- Method: MLE over a truncated Merton mixture PDF

The likelihood uses a Poisson-weighted Gaussian mixture:

`f(x) = sum_{n=0..n_max-1} exp(-lambda*dt) (lambda*dt)^n / n! * N(x; mu_n, sigma_n^2)`

where `mu_n` and `sigma_n^2` are Merton-adjusted jump moments for jump count `n`.

Output parameters are annualized `[sigma, lambda, mu_j, delta_j]`.

### Phase B: Runtime Valuation And Adaptation (C++)

Current runtime implementation in `cpp/` is:

- `OnlineMertonCalibrator::update_tick(...)` for rolling return ingestion
- `OnlineMertonCalibrator::maybe_update_params()` for gated local online updates
- `OnlineMertonCalibrator::fair_value(...)` for fast expectation pricing
- `OnlineMertonCalibrator::fair_value_quantlib(...)` as a QuantLib-based helper path

Important correction: the repo does **not** currently price with a full QuantLib
`Merton76Process` engine in the hot loop. QuantLib is integrated through the helper
forward/carry path (`fair_value_quantlib`) while the main low-latency loop uses the
custom calibrator/pricer logic.

### Phase C: Python Integration Layer

- C++ is exposed to Python via `pybind11`
- Binding code is in `cpp/src/python_module_entry.cpp`
- Member/field exposure is generated through C++26 compile-time reflection helpers in
  `cpp/include/reflection_engine.hpp`

The ProfitView strategy (`profitview_merton_signal.py`) consumes this module directly.

## Typical Workflow

1. **Offline calibration (Notebook)**
   - Load historical data (BitMEX API or local Parquet trades)
   - Build returns (resampled bars)
   - Run MLE to estimate `sigma, lambda, mu_j, delta_j`

2. **Build C++ module (optional but recommended for runtime adaptation)**
   - In `cpp/`, run `just test`
   - Produces and validates `cpp/build/merton_online_calibrator.so`

3. **Run strategy**
   - Use `profitview_merton_signal.py` in ProfitView
   - Feed live ticks to the calibrator
   - Compare theoretical value vs market price in real time

## Environment Configuration (`.env`)

1. Copy `.env.example` to `.env`
2. Set `MERTON_MARKET_MAKER_DATA_PATH` to your local parquet directory
3. Notebook uses this value if present; all other defaults remain in code

## C++ Module (`cpp/`)

The C++ module provides:

- rolling tick ingestion (`update_tick`)
- online parameter updates (`maybe_update_params`)
- fair-value calculation (`fair_value`)

Bindings are generated via compile-time reflection + pybind11 entrypoint.

For full C++ design/build/binding details, see [`cpp/README.md`](cpp/README.md).

### Quick commands

```bash
cd cpp
just test
```

This builds in Docker (host mode) or locally in the container (container mode), then runs a module smoke test.

The p2996 Clang compiler can also be used ad-hoc: `just shell` for an interactive bash, `just clang-pp -- ...` or `cpp/merton-clang++ ...` to compile from the host. See [cpp/README.md](cpp/README.md) for details.

## Runtime Target

The Docker and module build are aligned with:

- Ubuntu 22.04
- Python 3.9 (ProfitView-compatible target)

## Notes

- If you use the compiled `.so`, validate/import it with Python 3.9-compatible runtime.
- Notebook calibration and C++ online adaptation can be used independently.
- `.env` is gitignored; keep only private local filesystem paths there.