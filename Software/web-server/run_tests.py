import sys
import subprocess
import os
from pathlib import Path


def check_dependencies():
    try:
        import pytest  # noqa: F401
        import httpx  # noqa: F401
        import websocket  # noqa: F401
        import msgpack  # noqa: F401
        import yaml  # noqa: F401

        return True
    except ImportError as e:
        print(f"Missing dependency: {e}")
        return False


def install_dependencies():
    print("Installing test dependencies...")
    subprocess.run(
        [sys.executable, "-m", "pip", "install", "-r", "requirements-test.txt"],
        check=True,
    )


def run_tests(args=None):
    if args is None:
        args = []

    os.environ["TESTING"] = "true"

    project_root = Path(__file__).parent
    if str(project_root) not in sys.path:
        sys.path.insert(0, str(project_root))

    cmd = [sys.executable, "-m", "pytest"]

    if not args:
        cmd.extend(
            [
                "-v",  # Verbose
                "--tb=short",  # Short traceback
                "--color=yes",  # Colored output
                "-x",  # Stop on first failure
                "--cov=.",  # Coverage for all modules
                "--cov-report=term-missing",  # Show missing lines
            ]
        )
    else:
        cmd.extend(args)

    cmd.append("tests/")

    result = subprocess.run(cmd)
    return result.returncode


def run_module_tests(module_name):
    """Run tests for a specific module"""
    test_map = {
        "models": ["tests/test_smoke.py::TestSmoke::test_shot_data_model"],
        "managers": [
            "tests/test_smoke.py::TestSmoke::test_connection_manager",
            "tests/test_smoke.py::TestSmoke::test_shot_store",
        ],
        "parsers": [
            "tests/test_smoke.py::TestSmoke::test_parser",
            "tests/test_shot_simulation.py::TestShotSimulation::test_parser_validation",
        ],
        "listeners": ["tests/test_activemq_integration.py"],
        "server": ["tests/test_api_endpoints.py"],
        "websocket": ["tests/test_websocket.py"],
    }

    if module_name not in test_map:
        print(f"Unknown module: {module_name}")
        print(f"Available modules: {', '.join(test_map.keys())}")
        return 1

    print(f"Running tests for module: {module_name}")
    return run_tests(["-v"] + test_map[module_name])


def main():
    print("=" * 60)
    print("PiTrac Web Server Test Suite")
    print("Python Modular Structure")
    print("=" * 60)
    print()

    if sys.version_info < (3, 9):
        print(
            f"Warning: Python {sys.version_info.major}.{sys.version_info.minor} detected."
        )
        print("Recommended: Python 3.9 or higher")

    if not check_dependencies():
        print("Test dependencies not found.")
        response = input("Install them? (y/n): ") if sys.stdin.isatty() else "n"
        if response.lower() == "y":
            install_dependencies()
        else:
            print("Cannot run tests without dependencies.")
            return 1

    args = sys.argv[1:]

    if "--ci" in args:
        print("CI Mode: Running tests for continuous integration")
        args = [
            "--quiet",
            "--tb=line",
            "--no-header",
            "--cov=.",
            "--cov-report=xml",
            "--cov-report=term:skip-covered",
            "--cov-fail-under=60",  # Minimum coverage threshold
            "--junitxml=test-results.xml",
        ]

    elif "--quick" in args:
        print("Quick Mode: Running unit tests only")
        args = ["-m", "unit", "-v", "--tb=short"]

    elif "--full" in args:
        print("Full Mode: Complete test suite with coverage")
        args = ["-v", "--cov=.", "--cov-report=html", "--cov-report=term-missing"]

    elif "--module" in args:
        module_idx = args.index("--module")
        if module_idx + 1 < len(args):
            module_name = args[module_idx + 1]
            return run_module_tests(module_name)
        else:
            print("Error: --module requires a module name")
            return 1

    elif "--integration" in args:
        print("Integration Mode: Running integration tests only")
        args = ["-m", "integration", "-v", "--tb=short"]

    elif "--watch" in args:
        print("Watch Mode: Auto-running tests on file changes")
        try:
            subprocess.run(
                [
                    "pytest-watch",
                    "--clear",
                    "--wait",
                    "--runner",
                    "pytest -x --tb=short",
                ]
            )
            return 0
        except FileNotFoundError:
            print("pytest-watch not installed. Install with: pip install pytest-watch")
            return 1

    elif "--smoke" in args:
        print("Smoke Test Mode: Basic functionality check")
        args = ["tests/test_smoke.py", "-v", "--tb=short"]

    print("Running tests...")
    print("-" * 60)
    return run_tests(args)


if __name__ == "__main__":
    sys.exit(main())
