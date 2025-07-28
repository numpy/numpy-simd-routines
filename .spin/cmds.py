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
def generate(*, force):
    spin.util.run(
        ["python", str(rootdir / "tools" / "sollya" / "generate.py")]
        + (["--force"] if force else []),
    )
