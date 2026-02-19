// -----------------------------------------------------------------------------
// merton_online_calibrator.cpp
// -----------------------------------------------------------------------------
//
// Implements OnlineMertonCalibrator: a real-time Merton jump-diffusion
// calibrator and fair-value pricer for BitMEX perpetuals.
//
// Process: dS_t/S_t = (r - q - lambda*k)*dt + sigma*dW_t + (J-1)*dN_t
//   - sigma: diffusion volatility
//   - lambda: jump intensity (jumps per year)
//   - mu_j, delta_j: log-jump size ~ N(mu_j, delta_j^2)
//   - k = E[J-1] = exp(mu_j + 0.5*delta_j^2) - 1
//
// Flow:
//   1. update_tick(price, ts_us): ingest ticks, compute log returns, roll buffer
//   2. maybe_update_params(): gated MLE coordinate search over rolling returns
//   3. fair_value(s0, q, T, r): E[S_T] = S0 * exp((r - q - lambda*k)*T)
// -----------------------------------------------------------------------------

#include "merton_online_calibrator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <ql/handle.hpp>
#include <ql/settings.hpp>
#include <ql/shared_ptr.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/yieldtermstructure.hpp>
#include <ql/time/date.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>

namespace merton {

namespace {

// Seconds in one year (used to convert dt_us -> dt_years).
constexpr double kSecsPerYear = 365.25 * 24.0 * 3600.0;
// 1/sqrt(2*pi) for standard normal PDF: phi(z) = (1/sqrt(2pi)) * exp(-z^2/2)
constexpr double kInvSqrt2Pi = 0.3989422804014326779399460599343818684759;

/// Guards against log(0) or log(negative); clamps to kFloor before log.
double safe_log(double x) {
    constexpr double kFloor = 1e-300;
    return std::log(std::max(x, kFloor));
}

/// Jump compensator k = E[J-1] = exp(mu_j + 0.5*delta_j^2) - 1.
/// Used in drift and in fair-value formula.
double jump_compensator(double mu_j, double delta_j) {
    return std::exp(mu_j + 0.5 * delta_j * delta_j) - 1.0;
}

/// Standard normal PDF phi(z) = (1/sqrt(2pi)) * exp(-z^2/2).
double standard_normal_pdf(double z) {
    return kInvSqrt2Pi * std::exp(-0.5 * z * z);
}

/// Poisson weight for n jumps in interval of length lambda_dt:
///   w_n = exp(-lambda_dt) * (lambda_dt)^n / n!
/// Computed incrementally to avoid factorial overflow.
double poisson_weight(std::size_t n, double lambda_dt) {
    if (n == 0) {
        return std::exp(-lambda_dt);
    }
    double w = std::exp(-lambda_dt);
    for (std::size_t i = 1; i <= n; ++i) {
        w *= lambda_dt / static_cast<double>(i);
    }
    return w;
}

}  // namespace

// -----------------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------------

OnlineMertonCalibrator::OnlineMertonCalibrator(MertonParams initial, CalibratorConfig config)
    : params_(clamp_params(initial)), config_(config) {}

// -----------------------------------------------------------------------------
// Tick ingestion
// -----------------------------------------------------------------------------
//
// Pushes a new (price, timestamp) pair into the calibrator. On success:
//   - Computes log return r = log(price / last_price)
//   - Appends (r, dt_us) to rolling buffers
//   - Trims to window_size (FIFO)
//   - Increments returns_since_last_update_ for gating maybe_update_params()
//
// Returns true iff a valid return was appended. Returns false if:
//   - price <= 0
//   - first tick (no previous price)
//   - dt_us <= 0 (duplicate or backwards time)
//   - r not finite (e.g. zero price)
// -----------------------------------------------------------------------------

bool OnlineMertonCalibrator::update_tick(double price, std::int64_t epoch_us) {
    if (!(price > 0.0)) {
        return false;
    }
    if (!last_price_.has_value() || !last_ts_us_.has_value()) {
        last_price_ = price;
        last_ts_us_ = epoch_us;
        return false;
    }

    const std::int64_t dt_us = epoch_us - *last_ts_us_;
    if (dt_us <= 0) {
        last_price_ = price;
        last_ts_us_ = epoch_us;
        return false;
    }

    const double r = std::log(price / *last_price_);
    if (!std::isfinite(r)) {
        last_price_ = price;
        last_ts_us_ = epoch_us;
        return false;
    }

    returns_.push_back(r);
    dt_us_.push_back(dt_us);
    if (returns_.size() > config_.window_size) {
        returns_.pop_front();
        dt_us_.pop_front();
    }

    ++returns_since_last_update_;
    last_price_ = price;
    last_ts_us_ = epoch_us;
    return true;
}

// -----------------------------------------------------------------------------
// Online recalibration (MLE via coordinate search)
// -----------------------------------------------------------------------------
//
// Runs only when:
//   - returns_.size() >= min_points_for_update
//   - returns_since_last_update_ >= update_every_n_returns
//
// Uses dt = median of dt_us_ (in years) as representative time step.
// Coordinate-search: for each param, try +/- step; keep if NLL improves by
// improvement_tol. If no improvement in a round, halve all steps. Repeats
// coordinate_steps rounds.
//
// Step sizes: 8% of sigma, 10% of lambda, 25% of |mu_j|, 20% of delta_j,
// with floors to avoid degenerate steps.
//
// Returns true iff any parameter actually changed.
// -----------------------------------------------------------------------------

bool OnlineMertonCalibrator::maybe_update_params() {
    if (returns_.size() < config_.min_points_for_update) {
        return false;
    }
    if (returns_since_last_update_ < config_.update_every_n_returns) {
        return false;
    }

    returns_since_last_update_ = 0;
    const double dt = estimate_dt_years();
    if (!(dt > 0.0)) {
        return false;
    }

    MertonParams best = params_;
    double best_nll = neg_log_likelihood(best, dt);

    // Adaptive step sizes: percentage of current param with floors
    MertonParams step{
        std::max(0.02, best.sigma * 0.08),
        std::max(0.10, best.lambda * 0.10),
        std::max(0.002, std::abs(best.mu_j) * 0.25),
        std::max(0.002, best.delta_j * 0.20),
    };

    for (std::size_t iter = 0; iter < config_.coordinate_steps; ++iter) {
        bool improved = false;

        // Try candidate; accept if NLL improves by at least improvement_tol
        auto try_param = [&](const MertonParams& candidate) {
            const MertonParams c = clamp_params(candidate);
            const double nll = neg_log_likelihood(c, dt);
            if (std::isfinite(nll) && (best_nll - nll) > config_.improvement_tol) {
                best = c;
                best_nll = nll;
                improved = true;
            }
        };

        MertonParams c = best;
        c.sigma += step.sigma; try_param(c);
        c = best; c.sigma -= step.sigma; try_param(c);

        c = best; c.lambda += step.lambda; try_param(c);
        c = best; c.lambda -= step.lambda; try_param(c);

        c = best; c.mu_j += step.mu_j; try_param(c);
        c = best; c.mu_j -= step.mu_j; try_param(c);

        c = best; c.delta_j += step.delta_j; try_param(c);
        c = best; c.delta_j -= step.delta_j; try_param(c);

        // Shrink steps if no improvement this round (refinement)
        if (!improved) {
            step.sigma *= 0.5;
            step.lambda *= 0.5;
            step.mu_j *= 0.5;
            step.delta_j *= 0.5;
        }
    }

    // Report change if any param moved beyond floating-point noise
    const bool changed =
        (std::abs(best.sigma - params_.sigma) > 1e-12) ||
        (std::abs(best.lambda - params_.lambda) > 1e-12) ||
        (std::abs(best.mu_j - params_.mu_j) > 1e-12) ||
        (std::abs(best.delta_j - params_.delta_j) > 1e-12);

    params_ = best;
    return changed;
}

// -----------------------------------------------------------------------------
// Fair value (analytic)
// -----------------------------------------------------------------------------
//
// E[S_T] = S0 * exp((r - q - lambda*k)*T)
// with k = jump_compensator(mu_j, delta_j). No QuantLib in hot path.
// -----------------------------------------------------------------------------

double OnlineMertonCalibrator::fair_value(double s0, double q_annual, double t_years, double r) const {
    const double k = jump_compensator(params_.mu_j, params_.delta_j);
    const double drift = r - q_annual - params_.lambda * k;
    return s0 * std::exp(drift * t_years);
}

// -----------------------------------------------------------------------------
// Fair value (QuantLib-based helper)
// -----------------------------------------------------------------------------
//
// Uses flat r/q curves and F = S0 * Dq(T)/Dr(T), then applies Merton jump
// adjustment: F * exp(-lambda*k*T). Used for validation or when curve objects
// are needed; not used in the hot path.
// -----------------------------------------------------------------------------

double OnlineMertonCalibrator::fair_value_quantlib(double s0, double q_annual, double t_years, double r) const {
    if (!(s0 > 0.0)) {
        return s0;
    }

    using namespace QuantLib;
    const Date today = Date::todaysDate();
    Settings::instance().evaluationDate() = today;
    const DayCounter dc = Actual365Fixed();

    const Integer days = std::max<Integer>(1, static_cast<Integer>(std::llround(std::max(t_years, 1e-8) * 365.25)));
    const Date maturity = today + days;
    const Time t = dc.yearFraction(today, maturity);
    if (!(t > 0.0)) {
        return s0;
    }

    const auto r_curve = Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(today, r, dc));
    const auto q_curve = Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(today, q_annual, dc));

    // Forward from discount factors: F = S0 * Dq(T) / Dr(T) (no-jump forward)
    const double forward = s0 * (q_curve->discount(maturity) / r_curve->discount(maturity));

    // Merton jump compensator adjustment: forward * exp(-lambda*k*T)
    const double k = jump_compensator(params_.mu_j, params_.delta_j);
    return forward * std::exp(-params_.lambda * k * t);
}

// -----------------------------------------------------------------------------
// Merton jump-diffusion PDF (truncated Poisson-Gaussian mixture)
// -----------------------------------------------------------------------------
//
// f(x) = sum_{n=0}^{n_max} P(N=n) * phi((x - mu_n) / sigma_n) / sigma_n
// where:
//   drift = (-lambda*k - 0.5*sigma^2)*dt
//   mu_n = drift + n*mu_j
//   var_n = sigma^2*dt + n*delta_j^2
//   P(N=n) = exp(-lambda*dt) * (lambda*dt)^n / n!
//
// Returns max(pdf, 1e-300) to avoid zero in neg_log_likelihood.
// -----------------------------------------------------------------------------

double OnlineMertonCalibrator::merton_pdf(double x, const MertonParams& p, double dt_years) const {
    const double lambda_dt = p.lambda * dt_years;
    const double k = jump_compensator(p.mu_j, p.delta_j);
    const double drift = (-p.lambda * k - 0.5 * p.sigma * p.sigma) * dt_years;

    double pdf = 0.0;
    for (std::size_t n = 0; n < config_.n_max; ++n) {
        // Conditional mean and variance given n jumps
        const double mu_n = drift + static_cast<double>(n) * p.mu_j;
        const double var_n = p.sigma * p.sigma * dt_years + static_cast<double>(n) * p.delta_j * p.delta_j;
        if (var_n <= 0.0) {
            continue;
        }
        const double sigma_n = std::sqrt(var_n);
        const double z = (x - mu_n) / sigma_n;
        pdf += poisson_weight(n, lambda_dt) * (standard_normal_pdf(z) / sigma_n);  // N(mu_n, var_n) contribution
    }
    return std::max(pdf, 1e-300);
}

// -----------------------------------------------------------------------------
// Negative log-likelihood
// -----------------------------------------------------------------------------
//
// NLL(p) = -sum_i log f(r_i | p, dt) over rolling returns_. Invalid params
// (sigma <= 0, lambda < 0, delta_j <= 0) return +infinity.
// -----------------------------------------------------------------------------

double OnlineMertonCalibrator::neg_log_likelihood(const MertonParams& p, double dt_years) const {
    if (!(p.sigma > 0.0) || !(p.lambda >= 0.0) || !(p.delta_j > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    double nll = 0.0;
    for (const double r : returns_) {
        nll -= safe_log(merton_pdf(r, p, dt_years));
    }
    return nll;
}

// -----------------------------------------------------------------------------
// Parameter clamping
// -----------------------------------------------------------------------------
//
// Keeps params in sensible ranges: sigma [0.05, 3], lambda [0.01, 40],
// mu_j [-0.5, 0.5], delta_j [0.01, 1]. Applied on construction and after
// each candidate in coordinate search.
// -----------------------------------------------------------------------------

MertonParams OnlineMertonCalibrator::clamp_params(const MertonParams& p) const {
    MertonParams out = p;
    out.sigma = std::clamp(out.sigma, 0.05, 3.0);
    out.lambda = std::clamp(out.lambda, 0.01, 40.0);
    out.mu_j = std::clamp(out.mu_j, -0.5, 0.5);
    out.delta_j = std::clamp(out.delta_j, 0.01, 1.0);
    return out;
}

// -----------------------------------------------------------------------------
// Representative time step (years)
// -----------------------------------------------------------------------------
//
// Returns median(dt_us) converted to years. Used as dt_years in merton_pdf
// and NLL for the rolling window.
// -----------------------------------------------------------------------------

double OnlineMertonCalibrator::estimate_dt_years() const {
    if (dt_us_.empty()) {
        return 0.0;
    }
    std::vector<std::int64_t> s(dt_us_.begin(), dt_us_.end());
    std::sort(s.begin(), s.end());
    const std::int64_t median_us = s[s.size() / 2];
    return static_cast<double>(median_us) / 1e6 / kSecsPerYear;
}

}  // namespace merton

