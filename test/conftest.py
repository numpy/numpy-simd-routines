import pytest
import numpy_sr as sr


@pytest.fixture(params=list(sr.targets.values()), autouse=True, scope="function")
def setup_target(request):
    target = request.param
    _globals = request.function.__globals__
    _globals.update(target.intrinsics)
    yield target
