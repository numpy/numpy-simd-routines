from itertools import chain
from generator import c_array, c_header, sollya


def gen_approx(float_size, func, func_driv):
    n = 1 << (8 if float_size == 32 else 9)
    trunc_upper, cast = (
        ("p2", "single") if float_size == 32 else ("round(p2, 24, RZ)", "double")
    )
    return sollya(
        f"""
        prec = {float_size*4}; 
        display = hexadecimal;
        cast = {cast}(x);
        func = {func}(x);
        scale = 2.0 * pi / {n};
        for e from 0 to {n - 1} do {{ 
            theta = e * scale;
            p1 = cast(func(theta));
            p2 = cast(func(theta) - p1);
            p2 = {trunc_upper};
            deriv = {func_driv}(theta);
            k = ceil(log2(abs(deriv)));
            if (deriv < 0) then  {{
                k = -k;
            }};
            p3 = 2.0^k;
            p0 = cast(deriv - p3);
            p0;p3;p1;p2;
        }};
    """
    )


print(
    c_header(
        "template <typename T> constexpr T kSinApproxTable[] = {};",
        "template <typename T> constexpr T kCosApproxTable[] = {};",
        *chain.from_iterable(
            [
                (
                    f"template <> constexpr {T} kSinApproxTable<{T}>[] = "
                    + c_array(gen_approx(size, "sin", "cos"), col=4, sfx=sfx),
                    f"template <> constexpr {T} kCosApproxTable<{T}>[] = "
                    + c_array(gen_approx(size, "cos", "-sin"), col=4, sfx=sfx),
                )
                for size, T, sfx in [(32, "float", "f"), (64, "double", "")]
            ]
        ),
        namespace="npsr::trig::data",
    )
)
