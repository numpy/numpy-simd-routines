#!/usr/bin/env python3
import subprocess
import tempfile
import os
import pathlib
import glob
import sys
from itertools import chain

curdir = pathlib.Path(__file__).parent
rootdir = curdir.parent.parent
sys.path.insert(0, str(curdir))


def sollya(sollya_file, out, encoding="utf-8"):
    print(f"Executing {sollya_file}...")
    rout = str(pathlib.Path(out).resolve().relative_to(rootdir))
    rsoll = str(pathlib.Path(sollya_file).resolve().relative_to(rootdir))
    guard_name = rout.upper().replace("/", "_").replace(".", "_").replace("-", "_")

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".py", delete=False, encoding=encoding
    ) as f:
        pycode_temp = f.name

    pre = "\n".join(
        [
            f'SOURCE_GUARD_NAME = "{guard_name}";',
            f'SOURCE_FILE_PATH = "{rsoll}";',
            f'OUTPUT_FILE_PATH = "{out}";',
            f'PYTEMP_FILE_PATH = "{pycode_temp}";',
            f'execute("{curdir/"core.sol"}");',
        ]
    )

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".sol", delete=False, encoding=encoding
    ) as f:
        f.write(pre + "\n")
        with open(sollya_file, "r", encoding=encoding) as rf:
            f.write(rf.read().strip())
        f.write("quit;\n")
        sollya_temp = f.name

    try:
        # Execute Sollya with temp file
        result = subprocess.run(
            ["sollya", sollya_temp], cwd=pathlib.Path(sollya_file).parent
        )
        if result.returncode != 0:
            raise RuntimeError(f"Sollya execution failed with code {result.returncode}")
    finally:
        # Clean up temp file
        os.unlink(sollya_temp)
        os.unlink(pycode_temp)


def main(force):
    print("Generating sollya files...")
    path = rootdir / "npsr"
    exts = ["*.h", "*.py", "*.csv"]
    from_exts = [f"{ext}.sol" for ext in exts]
    patterns = [f"{path}/**/data/{ext}" for ext in from_exts]
    files = list(chain.from_iterable(glob.glob(p, recursive=True) for p in patterns))

    for f in files:
        out = f
        for frm, to in zip(from_exts, exts):
            out = out.replace(frm[1:], to[1:])
        if not force and pathlib.Path(out).exists():
            print(f"Skipping {out}, file already exists")
            continue
        sollya(f, out)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate C++ headers/python templates from Python scripts."
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Force regenerate all files, even if they already exist.",
    )
    args = parser.parse_args()
    main(force=args.force)
