"""File pickers hold emulation time through selection and cancellation."""
import json
import zipfile

import pytest

from .test_explorers import click, save_file


def open_menu_picker(emu, menu, label):
    click(emu, "##MainMenuBar", menu)
    item = next(i for i in emu.list_items() if i["label"] == label)
    click(emu, item["window"], label)


def assert_frozen(emu):
    before = emu.get_sim_state()
    assert before["fileDialogPaused"]
    assert not before["running"]
    emu.wait_frames(20)
    after = emu.get_sim_state()
    assert after["emulationTick"] == before["emulationTick"]
    return before["emulationTick"]


@pytest.fixture
def picker(emu):
    emu.send("file_dialog_builtin on")
    emu.resume()
    emu.wait_frames(5)
    yield emu
    emu.send("key escape")
    emu.wait_frames(5)
    emu.resume()


@pytest.mark.parametrize("menu,label,window", [
    ("File", "Save State...", "Save File"),
    ("File", "Load State...", "Open File"),
    ("File", "Open Image...", "Open File"),
    ("Record", "Record Audio...", "Save File"),
    ("Record", "Record Raw Audio...", "Save File"),
    ("Record", "Record SAP Type R...", "Save File"),
    ("Record", "Record VGM...", "Save File"),
    ("Tools", "Export ROM set...", "Select Folder"),
])
def test_picker_cancel_restores_running(picker, menu, label, window):
    open_menu_picker(picker, menu, label)
    assert_frozen(picker)
    click(picker, window, "Cancel")
    assert picker.get_sim_state()["running"]
    assert not picker.get_sim_state()["fileDialogPaused"]


@pytest.mark.parametrize("paused_before", [False, True])
def test_screenshot_picker_preserves_pause(picker, tmp_path, paused_before):
    if paused_before:
        picker.pause()
    picker.send("run_command Edit.SaveFrame")
    picker.wait_frames(5)
    tick = assert_frozen(picker)
    picker.send("screenshot " + str(tmp_path / "before.png"))
    save_file(picker, tmp_path / "frame.png")
    assert (tmp_path / "frame.png").read_bytes() == (tmp_path / "before.png").read_bytes()
    assert (tmp_path / "frame.png").is_file()
    state = picker.get_sim_state()
    assert state["running"] is (not paused_before)
    if paused_before:
        assert state["paused"]
        assert state["emulationTick"] == tick


def test_explicit_pause_during_picker_is_preserved(picker):
    picker.send("run_command Edit.SaveFrame")
    picker.wait_frames(5)
    assert_frozen(picker)
    picker.pause()
    click(picker, "Save File", "Cancel")
    assert picker.get_sim_state()["paused"]
    assert not picker.get_sim_state()["running"]


@pytest.mark.parametrize("paused_before", [False, True])
def test_save_state_captures_frozen_instant(picker, tmp_path, paused_before):
    if paused_before:
        picker.pause()
    open_menu_picker(picker, "File", "Save State...")
    tick = assert_frozen(picker)
    reference = tmp_path / "reference.atstate2"
    picker.send("save_state " + str(reference))
    path = tmp_path / "precise.atstate2"
    save_file(picker, path)
    assert path.is_file()
    with zipfile.ZipFile(reference) as before, zipfile.ZipFile(path) as after:
        assert before.read("memory.bin") == after.read("memory.bin")
        for kind in ("ATSaveStateCPU", "ATSaveStateAntic"):
            def hardware_state(archive):
                objects = json.loads(archive.read("savestate.json"))["objects"]
                return next(obj for obj in objects if obj["_type"] == kind)
            assert hardware_state(before) == hardware_state(after)
    state = picker.get_sim_state()
    assert state["running"] is (not paused_before)
    if paused_before:
        assert state["emulationTick"] == tick
        assert state["paused"]


def test_video_filename_picker_pauses(picker):
    open_menu_picker(picker, "Record", "Record Video...")
    click(picker, "Record Video", "Record")
    assert_frozen(picker)
    click(picker, "Save File", "Cancel")
    assert picker.get_sim_state()["running"]


def test_replacing_picker_keeps_pause_until_final_cancel(picker):
    picker.send("run_command Edit.SaveFrame")
    picker.wait_frames(5)
    tick = assert_frozen(picker)
    picker.send("run_command Edit.SaveFrame")
    picker.wait_frames(5)
    assert assert_frozen(picker) == tick
    picker.send("key escape")
    picker.wait_frames(5)
    assert picker.get_sim_state()["running"]


def test_cancel_keeps_preexisting_pause(picker):
    picker.pause()
    picker.send("run_command Edit.SaveFrame")
    picker.wait_frames(5)
    tick = assert_frozen(picker)
    click(picker, "Save File", "Cancel")
    state = picker.get_sim_state()
    assert state["paused"]
    assert not state["running"]
    assert state["emulationTick"] == tick
