from pytest import mark
from numpy_sr import Precise
from .data import large as large_data


@mark.parametrize(
    "prec, max_ulp",
    [
        (Precise(), 1),
        (Precise(kLowAccuracy=True), 4),
    ],
)
class TestSinCos:
    @mark.parametrize("lane_str", ["float64_t", "float32_t"])
    def test_sin_stress(self, prec, max_ulp, lane_str):
        data = getattr(large_data, f"sin_{lane_str}")
        for input, expected in data:
            actual = Sin(prec, input)
            assert_ulp(actual, expected, input, max_ulp=max_ulp)

    @mark.parametrize("lane_str", ["float64_t", "float32_t"])
    def test_cos_stress(self, prec, max_ulp, lane_str):
        data = getattr(large_data, f"cos_{lane_str}")
        for input, expected in data:
            actual = Cos(prec, input)
            assert_ulp(actual, expected, input, max_ulp=max_ulp)
