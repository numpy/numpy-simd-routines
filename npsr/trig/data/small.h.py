from generator import c_array, c_header, sollya


def gen_kpi16(float_size, func):
    cast = "single" if float_size == 32 else "double"
    return sollya(
        f"""
        prec = {float_size*3}; 
        display = hexadecimal;
        cast = {cast}(x);
        func = {func}(x);
        pi16 = pi / 16;
        for k from 0 to 15 do {{
            cast(func(k * pi16));
        }};
        """
    )


def gen_pack_func(clo, slo):
    combined = (clo & 0xFFFFFFFF00000000) | ((slo >> 32) & 0xFFFFFFFF)
    return f"0x{combined:016x}"


def gen_kpi16_low_pack():
    ints = sollya(
        f"""
        prec = {64*3}; 
        display = hexadecimal;
        pi16 = pi / 16;
        for k from 0 to 15 do {{
            shi = double(sin(k * pi16));
            chi = double(cos(k * pi16));
            slo = double(sin(k * pi16) - shi);
            clo = double(cos(k * pi16) - chi);
            printdouble(clo);
            printdouble(slo);
        }};
        """,
        as_int=0x10,
    )
    packed = [gen_pack_func(clo, slo) for clo, slo in zip(ints[::2], ints[1::2])]
    ints = sollya(
        f"""
        prec = {64*3}; 
        display = hexadecimal;
        packed = [|{','.join(packed)}|];
        for k from 0 to 15 do {{
            double(packed[k]);
        }};
    """
    )
    return ints


print(
    c_header(
        "constexpr double kHiSinKPi16Table[] =" + c_array(gen_kpi16(64, "sin"), col=4),
        "constexpr double kHiCosKPi16Table[] =" + c_array(gen_kpi16(64, "cos"), col=4),
        "constexpr double kPackedLowSinCosKPi16Table[] ="
        + c_array(gen_kpi16_low_pack(), col=4),
        namespace="npsr::trig::data",
    )
)
