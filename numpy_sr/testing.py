"""Fractional-ULP array comparison plus reusable test-data helpers.

The MPFR oracle pairs its correctly rounded ``expected`` with a sub-ULP
``residual = (expected - exact)/ulp(expected)`` (numpy_sr/pyext-mpfr.h), so

    err(actual) = signed_ulp_distance(actual, expected) + residual

measures against the *exact* value, allowing fractional ``maxulp`` bounds.
On failure a table of the worst offenders (decimal + hex) is printed.
"""

from collections.abc import Iterator

import numpy as np
import numpy.typing as npt

# One full processing block; smaller buffers only exercise the scalar tail.
MIN_BLOCK = 4096

_INT = {np.dtype(np.float32): np.int32, np.dtype(np.float64): np.int64}


def _uint(dtype: npt.DTypeLike) -> np.dtype[np.unsignedinteger]:
    return np.dtype(f"u{np.dtype(dtype).itemsize}")


def _monotone_key(a: np.ndarray) -> np.ndarray:
    """Map floats to a monotone int64 axis where adjacent representable
    values differ by 1: key difference == signed ULP distance. Must stay
    int64 -- a ~2^63 float64 key would lose the low bits. +-0 share a key."""
    itype = _INT[a.dtype]
    bits = a.view(itype).astype(np.int64)
    imin = np.int64(np.iinfo(itype).min)
    return np.where(bits < 0, imin - bits, bits)


def ulp_error(
    actual: np.ndarray,
    expected: np.ndarray,
    residual: np.ndarray | float = 0.0,
) -> np.ndarray:
    """Signed ULP error of ``actual`` vs the exact value behind ``expected``
    (the correctly rounded reference); omit ``residual`` for a plain integer
    distance. Matching NaNs score 0; a lone NaN scores inf."""
    actual = np.asarray(actual)
    expected = np.asarray(expected)
    if actual.dtype != expected.dtype:
        raise TypeError(f"dtype mismatch: {actual.dtype} vs {expected.dtype}")

    ka, kb = _monotone_key(actual), _monotone_key(expected)
    # int64 subtraction can wrap for huge (already failing) distances; use the
    # float difference there, where the lost low bits no longer matter.
    approx = ka.astype(np.float64) - kb.astype(np.float64)
    dist = np.where(
        np.abs(approx) > 2.0**53, approx, (ka - kb).astype(np.float64)
    )
    err = dist + residual

    a_nan, e_nan = np.isnan(actual), np.isnan(expected)
    err = np.where(a_nan & e_nan, 0.0, err)
    return np.where(a_nan ^ e_nan, np.inf, err)


def _report(
    actual: np.ndarray,
    expected: np.ndarray,
    err: np.ndarray,
    maxulp: float,
    x: np.ndarray | None,
    limit: int,
) -> str:
    """Aligned worst-offenders table for an assert_max_ulp failure."""
    aerr = np.abs(err)
    nbad = int((aerr > maxulp).sum())
    worst = np.argsort(aerr)[::-1][: min(limit, nbad)]

    def num(v):
        return f"{float(v):.9g} ({float(v).hex()})"

    rows = [
        ["idx", *(["x"] if x is not None else []), "actual", "expected", "ulp"]
    ]
    for i in worst:
        row = [str(int(i))]
        if x is not None:
            row.append(num(x[i]))
        row += [num(actual[i]), num(expected[i]), f"{err[i]:+.4f}"]
        rows.append(row)
    widths = [max(len(row[c]) for row in rows) for c in range(len(rows[0]))]
    lines = [
        "  ".join(v.ljust(w) for v, w in zip(r, widths, strict=True)).rstrip()
        for r in rows
    ]
    return (
        f"{nbad} / {err.size} elements exceed {maxulp} ULP "
        f"(max {aerr[worst[0]]:.4g} ULP)\n" + "\n".join(lines)
    )


def assert_max_ulp(
    actual: np.ndarray,
    expected: np.ndarray,
    maxulp: float,
    *,
    x: np.ndarray | None = None,
    residual: np.ndarray | float = 0.0,
    limit: int = 20,
) -> np.ndarray:
    """Assert every element of ``actual`` is within ``maxulp`` (may be
    fractional) ULP of the exact value behind ``expected``. Pass ``x`` and
    ``residual`` (from ``mpfr.<op>(x)``) for a reproducible report of the
    worst ``limit`` offenders. Returns the signed error array."""
    err = ulp_error(actual, expected, residual)
    if np.any(np.abs(err) > maxulp):
        raise AssertionError(
            _report(
                np.asarray(actual), np.asarray(expected), err, maxulp, x, limit
            )
        )
    return err


def scatter(
    rng: np.random.Generator, x: np.ndarray, values: npt.ArrayLike
) -> np.ndarray:
    """Overwrite distinct random lanes of ``x`` with ``values`` (so specials
    hit random SIMD lanes, not a fixed array); returns the lane indices."""
    idx = rng.permutation(x.size)[: np.size(values)]
    x[idx] = values
    return idx


def ulp_neighbors(
    values: npt.ArrayLike, dtype: npt.DTypeLike, span: int
) -> np.ndarray:
    """Every representable value within +-span ULP of each POSITIVE value
    (positive keeps the bit pattern int64-safe)."""
    base = np.asarray(values, dtype=dtype).view(_uint(dtype)).astype(np.int64)
    off = np.arange(-span, span + 1, dtype=np.int64)
    return (base[:, None] + off).astype(_uint(dtype)).view(dtype).ravel()


def random_exp_floats(
    rng: np.random.Generator,
    dtype: npt.DTypeLike,
    size: int,
    min_exp: int,
    max_exp: int | None = None,
) -> np.ndarray:
    """Log-uniform finite floats assembled from IEEE bits (no math ufuncs):
    random sign/mantissa, binade uniform in [min_exp, max_exp] (default: the
    largest finite binade, so no draw rounds to inf)."""
    dt, u = np.dtype(dtype), _uint(dtype)
    info = np.finfo(dtype)
    mbits = info.nmant
    ebits = dt.itemsize * 8 - mbits - 1
    bias = (1 << (ebits - 1)) - 1
    if max_exp is None:
        max_exp = info.maxexp - 1
    exp = rng.integers(min_exp + bias, max_exp + bias + 1, size, dtype=u)
    mant = rng.integers(0, 1 << mbits, size, dtype=u)
    sign = rng.integers(0, 2, size, dtype=u) << (dt.itemsize * 8 - 1)
    return (sign | (exp << mbits) | mant).view(dt)


def random_subnormals(
    rng: np.random.Generator, dtype: npt.DTypeLike, size: int
) -> np.ndarray:
    """Random subnormals from bits: biased exponent 0, nonzero mantissa,
    both signs."""
    dt, u = np.dtype(dtype), _uint(dtype)
    mant = rng.integers(1, 1 << np.finfo(dtype).nmant, size, dtype=u)
    sign = rng.integers(0, 2, size, dtype=u) << (dt.itemsize * 8 - 1)
    return (sign | mant).view(dt)


_F32_FINITE_END = 0x7F800000  # first non-finite bit pattern (+inf)


def finite_f32_chunks(chunk: int = 1 << 24) -> Iterator[np.ndarray]:
    """Yield every finite float32, positives then negatives, in bit order."""
    for sign in (0x00000000, 0x80000000):
        for start in range(0, _F32_FINITE_END, chunk):
            stop = min(start + chunk, _F32_FINITE_END)
            yield np.arange(sign + start, sign + stop, dtype=np.uint32).view(
                np.float32
            )
