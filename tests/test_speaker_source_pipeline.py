"""Production speaker_source C++ with passive host interfaces, not a player model."""
from pathlib import Path
import shutil
import subprocess
import pytest

esphome = pytest.importorskip("esphome")

ROOT = Path(__file__).resolve().parents[1]

wav_builds = sorted((ROOT / "esphome/.esphome/build").glob("*/managed_components/esphome__micro-wav"))
if not wav_builds:
    pytest.skip("needs an esphome build to have fetched micro-wav", allow_module_level=True)
WAV = wav_builds[0]


@pytest.fixture(scope="module")
def pipeline_exe(tmp_path_factory):
    host = ROOT / "tests/host/speaker_source"
    component = ROOT / "esphome/components/speaker_source"
    # Use the project's installed MediaSource contract; platform interfaces alone are mocked.
    installed = Path(esphome.__file__).parent.parent
    compiler = shutil.which("c++")
    assert compiler, "host C++ compiler required"
    exe = tmp_path_factory.mktemp("speaker-pipeline") / "pipeline_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    "-DUSE_ESP32", "-I", str(host), "-I", str(component), "-I", str(installed), "-I", str(WAV / "include"),
                    str(host / "pipeline_test.cpp"), str(component / "speaker_source_media_player.cpp"),
                    str(WAV / "src/wav_decoder.cpp"),
                    "-o", str(exe)], check=True)
    return exe


@pytest.mark.parametrize("scenario", ["eof_repeat", "repeat_flags", "stop", "replace", "volume", "repeat_all", "smart_to_stock", "idle_smart_to_stock", "pending_smart_to_stock", "pending_stock_reselection", "replace_idle_before_loop", "replace_preserves_other_pipeline", "stop_pending_smart"])
def test_speaker_pipeline(pipeline_exe, scenario):
    subprocess.run([str(pipeline_exe), scenario], check=True)


@pytest.mark.parametrize("channels", [1, 2])
@pytest.mark.parametrize("chunk", [1, 3, 43, 44, 55, 56, 4095, 4096, 4097])
def test_real_wav_pcm_loops(pipeline_exe, channels, chunk):
    subprocess.run([str(pipeline_exe), "wav", str(chunk), str(channels)], check=True)
