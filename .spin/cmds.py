import pathlib
import sys

import click

curdir = pathlib.Path(__file__).parent
rootdir = curdir.parent
toolsdir = rootdir / "tools"
sys.path.insert(0, str(toolsdir))


@click.command(help="Generate sollya c++/python based files")
@click.option("-f", "--force", is_flag=True, help="Force regenerate all files")
def sollya(*, force):
    import sollya  # type: ignore[import]

    sollya.main(force=force)
