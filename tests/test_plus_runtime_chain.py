"""Compile real owner loop/delay with actual runtime/cursor/controller headers."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_runtime_owner_chain(tmp_path):
 actual=(ROOT/"esphome/components/hatch_plus_power/hatch_plus_power.cpp").read_text()
 methods=[]
 for name in ("prepare","state_shutdown","led_busy","loop","publish_hardware_status_","delay"):
  signature=("bool" if name=="led_busy" else "void")+" HatchPlusPower::"+name+"("
  start=actual.index(signature);brace=actual.index("{",start);depth=1;end=brace+1
  while depth:
   depth+=(actual[end]=="{")-(actual[end]=="}");end+=1
  methods.append(actual[start:end])
 fixture=(ROOT/"tests/host/power_runtime_chain_test.cpp").read_text()
 source=tmp_path/"chain.cpp"
 source.write_text("#define pdMS_TO_TICKS(n) (n)\nvoid vTaskDelay(unsigned){}\n"+fixture.replace("// OWNER_METHODS","\n".join(methods)))
 exe=tmp_path/"chain"
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT),str(source),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
