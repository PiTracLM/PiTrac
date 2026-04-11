"""Run on Pi with: python3.13 test_jinja_path.py"""
import os
from pathlib import Path
from jinja2 import FileSystemLoader, Environment

print(f"cwd: {os.getcwd()}")
print(f"_BASE_DIR would be: {Path(__file__).resolve().parent}")
print()

for path in ["templates", str(Path(__file__).resolve().parent / "templates")]:
    loader = FileSystemLoader(path)
    print(f"input: {path!r}")
    print(f"stored searchpath: {loader.searchpath}")
    env = Environment(loader=loader)
    try:
        env.get_template("nonexistent.html")
    except Exception as e:
        print(f"error: {e}")
    print()
