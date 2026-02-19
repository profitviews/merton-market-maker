#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace jd {

struct MertonParams {
    double sigma = 0.44;
    double lambda = 20.0;
    double mu_j = 0.003;
    double delta_j = 0.01;
};

struct CalibratorConfig {
    std::size_t window_size = 4096;
    std::size_t min_points_for_update = 512;
    std::size_t n_max = 15;
    std::size_t update_every_n_returns = 128;
    std::size_t coordinate_steps = 3;
    double improvement_tol = 1e-6;
};

class OnlineMertonCalibrator {
public:
    OnlineMertonCalibrator(MertonParams initial, CalibratorConfig config = {});

    // Feed live prices. Returns true if a return was accepted.
    bool update_tick(double price, std::int64_t epoch_us);

    // Returns true if parameters were updated.
    bool maybe_update_params();

    // Compute fair value E[S_T] for horizon T (years).
    double fair_value(double s0, double q_annual, double t_years, double r = 0.0) const;
    // QuantLib-based helper using discount curves/day count for carry forward.
    double fair_value_quantlib(double s0, double q_annual, double t_years, double r = 0.0) const;

    const MertonParams& params() const { return params_; }
    std::size_t sample_count() const { return returns_.size(); }

private:
    double merton_pdf(double x, const MertonParams& p, double dt_years) const;
    double neg_log_likelihood(const MertonParams& p, double dt_years) const;
    MertonParams clamp_params(const MertonParams& p) const;
    double estimate_dt_years() const;

    MertonParams params_;
    CalibratorConfig config_;

    std::optional<double> last_price_;
    std::optional<std::int64_t> last_ts_us_;
    std::deque<double> returns_;
    std::deque<std::int64_t> dt_us_;
    std::size_t returns_since_last_update_ = 0;
};

}  // namespace jd

