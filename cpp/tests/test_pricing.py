import math

import pytest


@pytest.mark.pricing
def test_fair_value_methods_are_finite_and_positive(calibrator):
    price, _ = calibrator.feed_ticks()

    t_years = 8.0 / (365.25 * 24.0)
    fv = calibrator.fair_value(price, 0.10, t_years, 0.0)
    fv_ql = calibrator.fair_value_quantlib(price, 0.10, t_years, 0.0)

    assert math.isfinite(fv)
    assert math.isfinite(fv_ql)
    assert fv > 0.0
    assert fv_ql > 0.0
