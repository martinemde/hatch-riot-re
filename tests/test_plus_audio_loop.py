"""Production speaker_source C++ with passive host interfaces, not a player model."""
from pathlib import Path
import shutil
import subprocess
import pytest
ROOT = Path(__file__).resolve().parents[1]

@pytest.fixture(scope="module")
def pipeline_exe(tmp_path_factory):
    host = ROOT / "tests/host/speaker_source"
    component = ROOT / "esphome/components/speaker_source"
    # Use the project's installed MediaSource contract; platform interfaces alone are mocked.
    import esphome
    installed = Path(esphome.__file__).parent.parent
    wav = ROOT / "esphome/.esphome/build/hatch-plus-example/managed_components/esphome__micro-wav"
    assert (wav / "src/wav_decoder.cpp").is_file(), "build ESPHome hatch-plus-example config first to fetch micro_wav"
    compiler = shutil.which("c++")
    assert compiler, "host C++ compiler required"
    exe = tmp_path_factory.mktemp("speaker-pipeline") / "pipeline_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    "-DUSE_ESP32", "-I", str(host), "-I", str(component), "-I", str(installed), "-I", str(wav / "include"),
                    str(host / "pipeline_test.cpp"), str(component / "speaker_source_media_player.cpp"),
                    str(wav / "src/wav_decoder.cpp"),
                    "-o", str(exe)], check=True)
    return exe

@pytest.mark.parametrize("scenario", ["eof_repeat", "repeat_flags", "stop", "replace", "volume", "repeat_all", "smart_to_stock", "idle_smart_to_stock"])
def test_speaker_pipeline(pipeline_exe, scenario):
    subprocess.run([str(pipeline_exe), scenario], check=True)

@pytest.mark.parametrize("channels", [1, 2])
@pytest.mark.parametrize("chunk", [1, 3, 43, 44, 55, 56, 4095, 4096, 4097])
def test_real_wav_pcm_loops(pipeline_exe, channels, chunk):
    subprocess.run([str(pipeline_exe), "wav", str(chunk), str(channels)], check=True)
