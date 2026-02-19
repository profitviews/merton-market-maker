import pytest


@pytest.mark.smoke
def test_module_import_and_init(calibrator):
    assert calibrator.sample_count() == 0
