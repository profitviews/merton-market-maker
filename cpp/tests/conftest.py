import pytest

import merton_online_calibrator as moc


def build_calibrator() -> moc.OnlineMertonCalibrator:
    p = moc.MertonParams()
    p.sigma = 0.44
    setattr(p, "lambda", 20.0)
    p.mu_j = 0.003
    p.delta_j = 0.01

    cfg = moc.CalibratorConfig()
    cfg.window_size = 2048
    cfg.min_points_for_update = 64
    cfg.update_every_n_returns = 32
    cfg.n_max = 10
    cfg.coordinate_steps = 2
    return moc.OnlineMertonCalibrator(p, cfg)


def feed_ticks(cal: moc.OnlineMertonCalibrator) -> tuple[float, int]:
    ts = 1_700_000_000_000_000  # epoch in microseconds
    price = 68_000.0
    for i in range(200):
        price *= (1.0 + 0.00005 * (1 if (i % 2 == 0) else -1))
        ts += 5_000_000  # 5 seconds
        cal.update_tick(price, ts)
        cal.maybe_update_params()
    return price, ts


class CalibratorHarness:
    def __init__(self) -> None:
        self.cal = build_calibrator()

    def feed_ticks(self) -> tuple[float, int]:
        return feed_ticks(self.cal)

    def sample_count(self) -> int:
        return self.cal.sample_count()

    def params(self) -> moc.MertonParams:
        return self.cal.params()

    def fair_value(self, price: float, q_annual: float, t_years: float, r: float) -> float:
        return self.cal.fair_value(price, q_annual, t_years, r)

    def fair_value_quantlib(
        self, price: float, q_annual: float, t_years: float, r: float
    ) -> float:
        return self.cal.fair_value_quantlib(price, q_annual, t_years, r)


@pytest.fixture
def calibrator() -> CalibratorHarness:
    return CalibratorHarness()
