"""Native-owner facade preserves register/payload transaction boundaries."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def test_native_i2c_transport(tmp_path):
    exe = tmp_path / "native_transport"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                    str(ROOT / "tests/host/native_i2c_transport_test.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
