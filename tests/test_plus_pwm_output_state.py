from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_pwm_output_state(tmp_path):
 exe=tmp_path/"pwm"
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT),str(ROOT/"tests/host/pwm_output_state_test.cpp"),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
