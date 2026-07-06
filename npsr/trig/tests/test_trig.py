"""Trig suite: hypothesis sweeps of the simple range, exact special cases,
FP-exception checks, and stress/exhaustive runs vs the correctly rounded
``numpy_sr.mpfr`` oracle."""

from functools import cache

import numpy as np
from hypothesis import given, settings
from hypothesis import strategies as st
from hypothesis.extra import numpy as hnp
from pytest import mark, skip

from numpy_sr import Precise, fenv, mpfr
from numpy_sr.testing import (
    MIN_BLOCK,
    assert_max_ulp,
    finite_f32_chunks,
    random_exp_floats,
    random_subnormals,
    scatter,
    ulp_error,
    ulp_neighbors,
)

OPS = ["cos", "sin"]

# precise.h: high ~1 ULP, low 1-4 ULP.
HIGH = Precise()
LOW = Precise(kLowAccuracy=True)
PROFILES = [HIGH, LOW]

SPECIALS = [0.0, -0.0, np.inf, -np.inf, np.nan]

# Fast-path <-> Extended (Payne-Hanek) switch points from npsr/trig/inl.h;
# f64 low-accuracy cos keeps its own higher bound (~8388607*pi).
_THRESHOLDS = {
    np.float32: (1e4,),
    np.float64: (2.0**24, float.fromhex("0x1.921fb2200366fp+24")),
}


def _bound(target, prec, dtype) -> float:
    """Max tolerated ULP vs the exact value, just above the measured worst
    case (high: f32 ~0.60, f64 ~0.52; low: f32 ~2.43, f64 ~3.52). FMA shifts
    sin/cos < 0.15 ULP, so non-FMA targets get +0.5 headroom."""
    if prec == HIGH:
        return 1.0
    base = 2.5 if np.dtype(dtype) == np.float32 else 3.5
    return base if target.has_fma else base + 0.5


def _check(target, op, prec, x, expected=None, residual=None):
    """Assert ``target.<op>(x, accuracy=prec)`` is within ``_bound`` of the
    oracle; pass ``expected``/``residual`` to reuse a cached oracle run."""
    if expected is None:
        expected, residual = getattr(mpfr, op)(x)
    actual = getattr(target, op)(x, accuracy=prec)
    assert_max_ulp(
        actual, expected, _bound(target, prec, x.dtype), x=x, residual=residual
    )


# --- simple range ------------------------------------------------------------


@mark.parametrize("prec", PROFILES)
@mark.parametrize("op", OPS)
@settings(max_examples=100, deadline=None)
@given(data=st.data())
def test_simple(target, op, dtype, prec, data):
    elements = st.floats(
        min_value=-1000.0, max_value=1000.0, width=np.finfo(dtype).bits
    )
    x = data.draw(
        hnp.arrays(
            dtype, st.integers(MIN_BLOCK, MIN_BLOCK + 256), elements=elements
        )
    )
    _check(target, op, prec, x)


# --- special cases -----------------------------------------------------------


@mark.parametrize("prec", [*PROFILES, Precise(kNoExceptions=True)])
@mark.parametrize("op", OPS)
def test_special_cases(target, rng, op, dtype, prec):
    """sin(+-0) = +-0 sign-exact, cos(+-0) = 1, +-inf/NaN -> NaN, at random
    lanes; the values are prec-independent, so noexc is asserted too."""
    op_fn = getattr(target, op)
    n = int(rng.integers(MIN_BLOCK, 2 * MIN_BLOCK))
    x = rng.uniform(-1000.0, 1000.0, n).astype(dtype)
    vals = np.repeat(SPECIALS, rng.integers(1, 9, len(SPECIALS)))
    idx = scatter(rng, x, vals)

    y = op_fn(x, accuracy=prec)
    for i, val in zip(idx, vals, strict=True):
        got = y[i]
        if not np.isfinite(val):
            assert np.isnan(got), f"{op}({val}) at lane {i} -> {got}, want NaN"
        elif op == "sin":
            assert got == 0 and np.signbit(got) == np.signbit(val), (
                f"sin({val!r}) at lane {i} -> {got!r}, want sign-exact zero"
            )
        else:
            assert got == 1, f"cos({val!r}) at lane {i} -> {got}, want 1"

    # No spurious NaN from the vector path.
    finite = np.ones(n, bool)
    finite[idx] = False
    assert np.isfinite(y[finite]).all()


# --- FP exception flags ------------------------------------------------------


@mark.parametrize("op", OPS)
def test_fp_exceptions(target, rng, op, dtype):
    """FE_INVALID for inf inputs, no spurious flags otherwise. kNoExceptions
    does no fenv bookkeeping, so it is not asserted."""
    fn = getattr(target, op)
    n = int(rng.integers(MIN_BLOCK, 2 * MIN_BLOCK))

    # Tiny/huge/max magnitudes reach every path, incl. Payne-Hanek reduction.
    finite = rng.uniform(-1e4, 1e4, n).astype(dtype)
    scatter(
        rng, finite, np.array([0.0, 1e-30, 1e4, np.finfo(dtype).max], dtype)
    )
    for prec in PROFILES:
        with fenv() as f:
            fn(finite, accuracy=prec)
        assert f.errors == 0

    # Quiet NaN propagates silently.
    with fenv() as f:
        fn(np.full(n, np.nan, dtype=dtype), accuracy=HIGH)
    assert f.errors == 0

    # IEEE 754: sin/cos(+-inf) is invalid -- and nothing else.
    xinf = rng.uniform(-1000.0, 1000.0, n).astype(dtype)
    scatter(rng, xinf, rng.choice([np.inf, -np.inf], int(rng.integers(1, 31))))
    for prec in PROFILES:
        with fenv() as f:
            fn(xinf, accuracy=prec)
        assert f.errors == f.FE_INVALID != 0


# --- algorithm edge cases ----------------------------------------------------


def _edge_values(dtype):
    """Branch thresholds +-3 ULP, k*pi/2 zeros/extrema and k*pi/16 reduction
    points for small k (worst reduction cancellation), the tiny/subnormal
    end; both signs."""
    info = np.finfo(dtype)
    pos = np.concatenate(
        [
            np.array([0.0, info.tiny, info.smallest_subnormal, 1.0], dtype),
            (np.arange(1, 65) * (np.pi / 2)).astype(dtype),
            (np.arange(1, 33) * (np.pi / 16)).astype(dtype),
            ulp_neighbors(_THRESHOLDS[dtype], dtype, 3),
        ]
    )
    return np.concatenate([pos, -pos])


@mark.parametrize("prec", PROFILES)
@mark.parametrize("op", OPS)
def test_edge_cases(target, rng, op, dtype, prec):
    """Fast-suite guard; stress covers the ranges around these edges."""
    edges = _edge_values(dtype)
    n = max(MIN_BLOCK, 2 * edges.size)
    x = rng.uniform(-1000.0, 1000.0, n).astype(dtype)
    scatter(rng, x, edges)
    _check(target, op, prec, x)


# --- stress vs the MPFR oracle -----------------------------------------------

STRESS_SIZE = 1_000_000


def _gen_small(rng, dtype):
    return rng.uniform(-np.pi / 4, np.pi / 4, STRESS_SIZE).astype(dtype)


def _gen_medium(rng, dtype):
    return rng.uniform(-100.0, 100.0, STRESS_SIZE).astype(dtype)


def _gen_large(rng, dtype):
    # Up to the Extended (Payne-Hanek) thresholds.
    hi = 1e4 if dtype is np.float32 else 2.0**24
    return rng.uniform(-hi, hi, STRESS_SIZE).astype(dtype)


def _gen_huge(rng, dtype):
    # Log-uniform across the whole Extended-reduction range.
    return random_exp_floats(
        rng, dtype, STRESS_SIZE, 14 if dtype is np.float32 else 25
    )


def _gen_near_kpi2(rng, dtype):
    # Nearest floats to k*pi/2 and their +-1 ULP neighbours: worst reduction
    # cancellation. Small k = first fast-path periods, huge k = deep
    # Payne-Hanek.
    kmax = 2**24 if dtype is np.float32 else 2**52
    m = STRESS_SIZE // 3
    k = np.concatenate(
        [rng.integers(1, 4096, m // 2), rng.integers(1, kmax, m - m // 2)]
    )
    return ulp_neighbors((k * (np.pi / 2)).astype(dtype), dtype, 1)


def _gen_subnormal(rng, dtype):
    # sin(x)~=x, cos(x)~=1: catches flush-to-zero / underflow mishandling.
    return random_subnormals(rng, dtype, STRESS_SIZE)


def _gen_thresholds(rng, dtype):
    # +-2048 ULP around each branch switch point (both signs) plus a random
    # band spanning below/above, hammering any cross-algorithm discontinuity.
    thr = _THRESHOLDS[dtype]
    near = ulp_neighbors(thr, dtype, 2048)
    near = np.concatenate([near, -near])
    n = STRESS_SIZE - near.size
    band = rng.uniform(min(thr) * 0.5, max(thr) * 2.0, n)
    return np.concatenate(
        [near, (band * rng.choice([-1.0, 1.0], n)).astype(dtype)]
    )


STRESS = {
    "small": _gen_small,
    "medium": _gen_medium,
    "large": _gen_large,
    "huge": _gen_huge,
    "near_kpi2": _gen_near_kpi2,
    "subnormal": _gen_subnormal,
    "thresholds": _gen_thresholds,
}


# Unbounded on purpose: the 28 combos recur across every target/profile, far
# apart in test order, and each holds a 1M-element MPFR oracle run.
@cache
def _stress_case(seed, op, dtype, range_name):
    # Per-case rng derived from the root seed: identical data on every
    # target/profile/xdist worker.
    entropy = int.from_bytes(f"{op}/{range_name}".encode(), "little")
    rng = np.random.default_rng([seed, np.dtype(dtype).num, entropy])
    x = STRESS[range_name](rng, dtype)
    return x, *getattr(mpfr, op)(x)


@mark.stress
@mark.parametrize("prec", PROFILES)
@mark.parametrize("range_name", STRESS)
@mark.parametrize("op", OPS)
def test_stress(target, request, op, dtype, range_name, prec):
    x, expected, residual = _stress_case(
        request.config.npsr_seed, op, dtype, range_name
    )
    _check(target, op, prec, x, expected, residual)


# --- exhaustive float32 (opt-in: -m exhaustive) ------------------------------


@mark.exhaustive
@mark.parametrize("prec", PROFILES)
@mark.parametrize("op", OPS)
def test_exhaustive_f32(sr, op, prec):
    """Every finite float32 through every target. MPFR over 4e9 inputs would
    take hours, so the lib's own f64 kernel (itself MPFR-validated by the f64
    stress run) prescreens and only flagged elements get the oracle verdict.
    numpy ufuncs are off-limits as reference: this lib becomes numpy's."""
    f64_ref = next((t for t in sr.targets.values() if t.has_float64), None)
    if f64_ref is None:
        skip("no float64 target to prescreen against")
    for x in finite_f32_chunks():
        approx = getattr(f64_ref, op)(
            x.astype(np.float64), accuracy=HIGH
        ).astype(np.float32)
        for tgt in sr.targets.values():
            actual = getattr(tgt, op)(x, accuracy=prec)
            bad = np.abs(ulp_error(actual, approx)) > _bound(
                tgt, prec, np.float32
            )
            if bad.any():
                _check(tgt, op, prec, x[bad])
