"""Generate C++ headers/Python templates from Sollya scripts."""

import argparse
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Final
from dataclasses import dataclass


# ANSI color codes for terminal output
if sys.stdout.isatty():

    class Colors:
        GREEN = "\033[92m"
        YELLOW = "\033[93m"
        RED = "\033[91m"
        BLUE = "\033[94m"
        MAGENTA = "\033[95m"
        RESET = "\033[0m"
        BOLD = "\033[1m"
        DIM = "\033[2m"

else:

    class Colors:
        GREEN = ""
        YELLOW = ""
        RED = ""
        BLUE = ""
        MAGENTA = ""
        RESET = ""
        BOLD = ""
        DIM = ""


def print_colored(text: str, color: str = "", icon: str = "", indent: int = 0) -> None:
    """Print text with color, icon, and indentation."""
    prefix = "  " * indent
    print(f"{prefix}{color}{icon}{text}{Colors.RESET}")


def print_divider() -> None:
    """Print a visual divider."""
    print(f"{Colors.DIM}  {'─' * 82}{Colors.RESET}")


@dataclass
class ProcessResult:
    """Result of processing a single Sollya file."""

    sollya_file: Path
    output_file: Path
    success: bool = False
    duration: float = 0.0
    error: str | None = None


def format_duration(seconds: float) -> str:
    """Format duration in human-readable format."""
    match seconds:
        case s if s < 1:
            return f"{s * 1000:.0f}ms"
        case s if s < 60:
            return f"{s:.1f}s"
        case s:
            return f"{int(s // 60)}m {int(s % 60)}s"


def check_sollya_available() -> bool:
    """Check if Sollya is available in PATH."""
    try:
        return (
            subprocess.run(
                ["sollya", "--version"], capture_output=True, check=False
            ).returncode
            == 0
        )
    except FileNotFoundError:
        return False


def sollya(sollya_file: Path, output_file: Path) -> ProcessResult:
    """Process a Sollya file and generate output."""
    print_colored(f"▶ Processing {sollya_file}", Colors.BLUE)

    # Setup paths
    current_dir: Final = Path(__file__).parent
    root_dir: Final = current_dir.parent.parent

    res = ProcessResult(sollya_file, output_file)

    try:
        relative_output = output_file.resolve().relative_to(root_dir)
        relative_sollya = sollya_file.resolve().relative_to(root_dir)
    except ValueError as e:
        res.error = f"Path resolution error: {e}"
        return res

    guard_name = str(relative_output).upper().translate(str.maketrans("/.\\-", "____"))

    # Create temp files and process
    with (
        tempfile.NamedTemporaryFile(mode="w", suffix=".py", delete=False) as temp_py,
        tempfile.NamedTemporaryFile(mode="w", suffix=".sol", delete=False) as temp_sol,
    ):
        res.duration = time.time()
        try:
            # Write Sollya script
            temp_sol.write(
                f"""SOURCE_GUARD_NAME = "{guard_name}";
SOURCE_FILE_PATH = "{relative_sollya}";
OUTPUT_FILE_PATH = "{output_file}";
PYTEMP_FILE_PATH = "{temp_py.name}";
execute("{current_dir / "core.sol"}");
{sollya_file.read_text().strip()}
quit;
"""
            )
            temp_sol.flush()

            process = subprocess.Popen(
                ["sollya", temp_sol.name],
                cwd=sollya_file.parent,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )

            print_divider()

            has_error = False
            # Drain stdout as it is produced. Reading before wait() is required:
            # with stdout=PIPE, wait() deadlocks if Sollya (verbosity=4) fills
            # the OS pipe buffer before exiting.
            for line in process.stdout:
                # Sollya prints diagnostics as "Warning ...", "Error ..." or
                # "Information ..." at the start of a line (optionally followed
                # by a message number). Match the prefix rather than searching
                # the whole line to avoid false positives from paths/comments.
                head = line.casefold().lstrip()
                # warnings are treated as errors to detect NaNs; some syntax
                # errors are also only reported as warnings
                got_error = head.startswith(("warning", "error"))
                got_info = head.startswith("information")
                has_error = has_error or got_error

                color = (
                    Colors.RED if got_error else Colors.BLUE if got_info else Colors.DIM
                )
                icon = "│✗" if got_error else "│ℹ" if got_info else "│"
                print_colored(f"{icon} {line.rstrip()}", color, indent=1)

            process.wait()
            print_divider()
            res.duration = time.time() - res.duration

            if process.returncode != 0 or has_error:
                res.error = f"Sollya failed with code {process.returncode}"
                return res

            res.success = True
            return res

        finally:
            # Cleanup
            for path in [temp_sol.name, temp_py.name]:
                Path(path).unlink(missing_ok=True)


def find_sollya_files(root_dir: Path) -> list[tuple[Path, Path]]:
    """Find all Sollya files and their corresponding output files."""
    search_path: Final = root_dir / "npsr"
    patterns: Final = [
        search_path.glob(f"**/data/{ext}")
        for ext in ["*.h.sol", "*.py.sol", "*.csv.sol"]
    ]

    return sorted(
        [
            (sollya_file, sollya_file.with_suffix(""))
            for pattern in patterns
            for sollya_file in pattern
        ]
    )


def process_files(files: list[tuple[Path, Path]]) -> list[ProcessResult]:
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as executor:
        futures = {executor.submit(sollya, sf, of): (sf, of) for sf, of in files}
        results = []

        for future in as_completed(futures):
            result = future.result()
            results.append(result)

            # Print result
            color = Colors.GREEN if result.success else Colors.RED
            icon = "✓ " if result.success else "✗ "
            status = "Completed" if result.success else "Failed"
            file_name = result.output_file if result.success else result.sollya_file
            if not result.success:
                result.output_file.unlink(missing_ok=True)
            duration = format_duration(result.duration)

            print_colored(f"{icon}{status}: {file_name} ({duration})", color)

            if not result.success and result.error:
                print_colored(result.error, Colors.RED, indent=1)

        return results


def main(*, force: bool = False) -> None:
    """Generate all files from Sollya sources."""
    print_colored("🔧 Sollya Code Generator", Colors.BOLD)
    print_divider()

    # Check Sollya availability
    if not check_sollya_available():
        print_colored("✗ Error: Sollya not found in PATH", Colors.RED)
        print_colored("Please install Sollya: https://www.sollya.org/", indent=1)
        sys.exit(1)

    # Find files
    root_dir: Final = Path(__file__).parent.parent.parent
    all_files = find_sollya_files(root_dir)

    if not all_files:
        print_colored("⚠ No Sollya files found", Colors.YELLOW)
        return

    print(f"Found {Colors.BOLD}{len(all_files)}{Colors.RESET} Sollya files")

    # Partition files
    to_process = []
    skipped = []

    for sollya_file, output_file in all_files:
        (skipped if not force and output_file.exists() else to_process).append(
            (sollya_file, output_file)
        )

    # Show skipped files
    if skipped:
        print_colored(
            f"Skipping {len(skipped)} existing files (use -f to regenerate)", Colors.DIM
        )
        for _, output_file in skipped:
            print_colored(f"○ {output_file}", Colors.DIM, indent=1)

    if not to_process:
        print_colored("✓ All files up to date", Colors.GREEN)
        return

    print(f"Processing {Colors.BOLD}{len(to_process)}{Colors.RESET} files...")

    # Process files and measure time
    start_time = time.time()
    results = process_files(to_process)
    total_duration = time.time() - start_time

    # Summary statistics
    successful = sum(r.success for r in results)
    failed = len(results) - successful

    print_divider()
    print_colored("Summary:", Colors.BOLD)
    print_colored(f"✓ Success: {successful}", Colors.GREEN, indent=1)
    if failed > 0:
        print_colored(f"✗ Failed:  {failed}", Colors.RED, indent=1)
    if skipped:
        print_colored(f"○ Skipped: {len(skipped)}", Colors.DIM, indent=1)
    print_colored(f"⏱  Time:    {format_duration(total_duration)}", indent=1)

    if len(to_process) > 1:
        total_sequential_time = sum(r.duration for r in results)
        speedup = total_sequential_time / total_duration
        avg_time = format_duration(total_sequential_time / len(results))
        print_colored(f"⚡ Speedup: {speedup:.1f}x (avg {avg_time}/file)", indent=1)

    # Show errors
    if errors := [r for r in results if not r.success]:
        print_colored("Errors:", Colors.RED)
        for result in errors:
            print_colored(f"• {result.sollya_file}", indent=1)
            if result.error:
                print_colored(result.error, Colors.DIM, indent=2)


def cli() -> None:
    """Command line interface."""
    parser = argparse.ArgumentParser(
        description="Generate C++ headers/Python templates from Sollya scripts.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                  # Process new/modified files with Sollya output
  %(prog)s -f               # Force regenerate all files
        """,
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Force regenerate all files, even if they exist",
    )

    args = parser.parse_args()

    try:
        main(force=args.force)
    except KeyboardInterrupt:
        print_colored("⚠ Interrupted by user", Colors.YELLOW)
        sys.exit(1)


if __name__ == "__main__":
    cli()
