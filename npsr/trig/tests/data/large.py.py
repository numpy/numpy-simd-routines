from pathlib import Path
from generator import py_array, sollya, pad_list


def generate(func, prec):
    """Generate a large sin/cos table using Sollya."""
    return sollya(
        f"""
        prec = {200};
        display = hexadecimal;
        procedure trig_func(input) {{
            sc = {prec}(input);
            if (sc != infty && sc != -infty) then {{
                print{prec}(sc);
                print{prec}({func}(sc));
            }};
        }};
        """,
        from_path=Path(__file__).parent / "large.sollya",
    )


print("from numpy_sr import Vec, bitcast, float32_t, float64_t, uint32_t, uint64_t\n")

for func in ["sin", "cos"]:
    chunk_size = 512
    for prec, lane, ulane in [
        ("double", "float64_t", "uint64_t"),
        ("single", "float32_t", "uint32_t"),
    ]:
        data = pad_list(generate(func, prec), chunk_size)
        print(f"{func}_{lane} = [")
        for i in range(0, len(data), chunk_size):
            chunk = data[i : i + chunk_size]
            print(
                f"\n################ chunk {i} ################\n",
                "(",
                f"bitcast({lane}, Vec({ulane})(",
                py_array(chunk[::2], col=8),
                ")),",
                f"bitcast({lane}, Vec({ulane})(",
                py_array(chunk[1::2], col=8),
                "))",
                "),",
            )
        print("]")
