"""Package-level pytest wiring; ships in the package for --pyargs runs."""

import os
import random

import numpy as np
import pytest

import numpy_sr


def _resolve_seed(config):
    """Root seed for all random test data: the controller-drawn seed
    forwarded via xdist `workerinput` (all workers sweep identical data),
    then an `NPSR_SEED` override, then a fresh random seed. The summary
    prints the winner, so red runs replay with `NPSR_SEED=<value>`.

    `workerinput` lacks the key when this conftest registers after xdist
    spawned its workers (rootdir walk instead of `--pyargs numpy_sr`).
    """
    workerinput = getattr(config, "workerinput", None)
    if workerinput is not None and "npsr_seed" in workerinput:
        return workerinput["npsr_seed"]
    env = os.environ.get("NPSR_SEED")
    if env is not None:
        return int(env)
    return random.randrange(2**32)


def pytest_configure(config):
    # Registered here so the markers ship with the installed package.
    config.addinivalue_line(
        "markers",
        "stress: MPFR-validated random sweeps; deselect with -m 'not stress'",
    )
    config.addinivalue_line(
        "markers",
        "exhaustive: every-finite-float32 sweep; opt-in via -m exhaustive",
    )
    config.npsr_seed = _resolve_seed(config)


@pytest.hookimpl(optionalhook=True)
def pytest_configure_node(node):
    # xdist controller: hand the drawn seed to each worker before it spawns.
    node.workerinput["npsr_seed"] = node.config.npsr_seed


def _seed_banner(config):
    return (
        f"numpy_sr RNG seed = {config.npsr_seed} "
        f"(reproduce with NPSR_SEED={config.npsr_seed})"
    )


def pytest_report_header(config):
    # Source-tree runs only; under --pyargs the terminal summary shows it.
    return _seed_banner(config)


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    terminalreporter.write_sep("-", _seed_banner(config))


def pytest_collection_modifyitems(config, items):
    """Unlike opt-out `stress`, `exhaustive` runs only when -m names it."""
    if "exhaustive" in config.getoption("markexpr"):
        return
    skip = pytest.mark.skip(reason="opt-in; run with -m exhaustive")
    for item in items:
        if "exhaustive" in item.keywords:
            item.add_marker(skip)


def pytest_make_parametrize_id(config, val, argname):
    """Name Precise params by their profile label ('high'/'low'/...)."""
    return val.label if isinstance(val, numpy_sr.Precise) else None


def pytest_generate_tests(metafunc):
    """Parametrize `target` over every SIMD target, jointly with `dtype` so
    each target is paired only with the dtypes it has kernels for."""
    if "target" not in metafunc.fixturenames:
        return
    names = [n for n in ("target", "dtype") if n in metafunc.fixturenames]
    targets = list(numpy_sr.targets.values())
    if not targets:
        marks = pytest.mark.skip(
            reason="no npsr libraries in the installed numpy_sr package; "
            "run `spin build` (or `spin test`)"
        )
        metafunc.parametrize(
            ",".join(names), [pytest.param(*[None] * len(names), marks=marks)]
        )
    elif "dtype" in names:
        metafunc.parametrize(
            "target, dtype",
            [
                pytest.param(t, dt, id=f"{t.name}-{np.dtype(dt).name}")
                for t in targets
                for dt in t.dtypes
            ],
        )
    else:
        metafunc.parametrize("target", targets, ids=list(numpy_sr.targets))


@pytest.fixture(scope="session")
def rng(request):
    return np.random.default_rng(request.config.npsr_seed)


@pytest.fixture(scope="session")
def sr():
    """The numpy_sr module; skips if no kernels are installed."""
    if not numpy_sr.targets:
        pytest.skip("no npsr libraries; run `spin build` (or `spin test`)")
    return numpy_sr
