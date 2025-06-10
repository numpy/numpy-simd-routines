import os
import pathlib
import sys
import click
import spin
from spin.cmds import meson

curdir = pathlib.Path(__file__).parent
rootdir = curdir.parent


@click.command(help="Generate sollya python based files")
@click.option("-f", "--force", is_flag=True, help="Force regenerate all files")
@click.option("-s", "--sollya-path", help="Path to sollya")
def generate(*, force, sollya_path):
    spin.util.run(
        ["python", str(rootdir / "tools" / "generator.py")]
        + (["--force"] if force else []),
    )


@spin.util.extend_command(spin.cmds.meson.build)
def build(*, parent_callback, **kwargs):
    parent_callback(**kwargs)


@click.option(
    "-m",
    "markexpr",
    metavar="MARKEXPR",
    default="",
    help="Run tests with the given markers",
)
@click.option(
    "-m",
    "markexpr",
    metavar="MARKEXPR",
    default="",
    help="Run tests with the given markers",
)
@spin.util.extend_command(spin.cmds.meson.test)
def test(*, parent_callback, pytest_args, tests, markexpr, **kwargs):
    """
    By default, spin will run `-m 'not slow'`. To run the full test suite, use
    `spin test -m full`
    """  # noqa: E501
    if (not pytest_args) and (not tests):
        pytest_args = (
            "--pyargs",
            "numpy_sr",
        )

    if "-m" not in pytest_args:
        if markexpr != "full":
            pytest_args = ("-m", markexpr) + pytest_args

    kwargs["pytest_args"] = pytest_args
    parent_callback(**{"pytest_args": pytest_args, "tests": tests, **kwargs})


@spin.util.extend_command(meson.python)
def python(*, parent_callback, **kwargs):
    env = os.environ
    env["PYTHONWARNINGS"] = env.get("PYTHONWARNINGS", "all")

    parent_callback(**kwargs)


@click.command(context_settings={"ignore_unknown_options": True})
@click.argument("ipython_args", metavar="", nargs=-1)
@meson.build_dir_option
def ipython(*, ipython_args, build_dir):
    """💻 Launch IPython shell with PYTHONPATH set

    OPTIONS are passed through directly to IPython, e.g.:

    spin ipython -i myscript.py
    """
    env = os.environ
    env["PYTHONWARNINGS"] = env.get("PYTHONWARNINGS", "all")

    ctx = click.get_current_context()
    ctx.invoke(spin.cmds.meson.build)

    ppath = meson._set_pythonpath(build_dir)

    print(f'💻 Launching IPython with PYTHONPATH="{ppath}"')

    # In spin >= 0.13.1, can replace with extended command, setting `pre_import`
    preimport = (
        r"import numpy_sr as sr; "
        r"print(f'\nPreimported NumPy SIMD Routines Tests {sr.__version__} as sr')"
    )
    spin.util.run(
        ["ipython", "--ignore-cwd", f"--TerminalIPythonApp.exec_lines={preimport}"]
        + list(ipython_args)
    )
