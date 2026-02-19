import math

import pytest


@pytest.mark.params
def test_online_update_and_params_are_finite(calibrator):
    price, _ = calibrator.feed_ticks()

    assert calibrator.sample_count() > 0
    params = calibrator.params()
    assert math.isfinite(params.sigma)
    assert math.isfinite(getattr(params, "lambda"))
    assert math.isfinite(params.mu_j)
    assert math.isfinite(params.delta_j)
    assert price > 0
