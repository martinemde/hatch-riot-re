"""Compile actual concrete owner lifecycle methods against narrow ESPHome mocks."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_runtime_owner_lifecycle(tmp_path):
 actual=(ROOT/"esphome/components/hatch_plus_power/hatch_plus_power.cpp").read_text()
 methods=[]
 for name in ("prepare","state_shutdown","led_busy","loop","publish_hardware_status_","update","source_changed"):
  signature=("bool" if name=="led_busy" else "void")+" HatchPlusPower::"+name+("()" if name=="update" else "(")
  start=actual.index(signature);brace=actual.index("{",start);depth=1;end=brace+1
  while depth:
   depth+=(actual[end]=="{")-(actual[end]=="}");end+=1
  methods.append(actual[start:end])
 fixture=(ROOT/"tests/host/power_runtime_owner_test.cpp").read_text()
 source=tmp_path/"owner.cpp";source.write_text(fixture.replace("// OWNER_METHODS","\n".join(methods)))
 exe=tmp_path/"owner"
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT),str(source),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

def test_runtime_owner_setup_failures(tmp_path):
 actual=(ROOT/"esphome/components/hatch_plus_power/hatch_plus_power.cpp").read_text()
 start=actual.index("void HatchPlusPower::setup(");brace=actual.index("{",start);depth=1;end=brace+1
 while depth:
  depth+=(actual[end]=="{")-(actual[end]=="}");end+=1
 fixture=(ROOT/"tests/host/power_runtime_setup_test.cpp").read_text()
 source=tmp_path/"setup.cpp";source.write_text(fixture.replace("// OWNER_METHODS",actual[start:end]))
 exe=tmp_path/"setup"
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT),str(source),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
