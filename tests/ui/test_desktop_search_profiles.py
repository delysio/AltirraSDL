"""Desktop discovery and profile isolation regressions."""
import json
import pytest

from .harness import AltirraTestHarness
from .test_explorers import click, enter, xex_bytes


@pytest.fixture
def desktop(request, monkeypatch, tmp_path):
    (tmp_path / "config").mkdir()
    library_dir = tmp_path / "config" / "altirra"
    library_dir.mkdir()
    games = []
    for name in ("Alpha", "Beta"):
        image = tmp_path / (name + ".xex")
        image.write_bytes(xex_bytes())
        games.append({"displayName": name, "variants": [{"path": str(image), "type": "xex"}]})
    (library_dir / "gamelibrary.json").write_text(json.dumps({"version": 3, "games": games}))
    (library_dir / "settings.ini").write_text(
        '[User\\AltirraSDL\\GameLibrary]\n'
        '"SourceCount" = 1\n'
        f'"Source0.Path" = "{tmp_path}"\n'
        '"Source0.IsArchive" = 0\n'
        '"Source0.IsFile" = 0\n'
    )
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 900")
        emu.send("file_dialog_builtin on")
        emu.wait_frames(5)
        yield emu


def search(emu, query):
    emu.send("run_command UI.GlobalSearch")
    emu.wait_frames(3)
    enter(emu, "Search actions and settings", "##search", query)


def config(emu, category):
    emu.open_dialog("SystemConfig")
    emu.wait_frames(3)
    click(emu, "Configure System", category)


def create_profile(emu, name):
    emu.open_dialog("Profiles")
    emu.wait_frames(3)
    click(emu, "Profiles", "Add Profile")
    emu.send("send_text " + name)
    emu.wait_frames(3)
    click(emu, "Profiles", "Done")
    return next(p for p in emu.query_state()["state"]["profiles"]["items"] if p["name"] == name)


def select_combo(emu, window, label, option):
    click(emu, window, label)
    click(emu, "", option)


def switch_profile(emu, name):
    emu.close_dialog("SystemConfig")
    emu.open_dialog("Profiles")
    emu.wait_frames(3)
    click(emu, "Profiles", name)


def test_search_settings_and_keyboard(desktop):
    emu = desktop
    emu.send("run_command UI.GlobalSearch")
    emu.wait_frames(4)
    assert emu.get_dialog_state("GlobalSearch")
    enter(emu, "Search actions and settings", "##search", "Memory Size", True)
    assert emu.get_dialog_state("SystemConfig")
    assert emu.query_state()["state"]["configurationPage"] == 5
    assert any(i["label"] == "Memory Size" for i in emu.list_items())
    emu.close_dialog("SystemConfig")
    search(emu, "screen too small")
    assert any(i["label"] == "Display" for i in emu.list_items("Search actions"))
    emu.send("key escape")
    emu.wait_frames(3)
    assert not emu.get_dialog_state("GlobalSearch")


def test_search_actions_and_no_matches(desktop):
    search(desktop, "Game Library")
    click(desktop, "Search actions", "Game Library")
    assert desktop.get_dialog_state("GameLibrary")
    desktop.close_dialog("GameLibrary")
    search(desktop, "Record Audio")
    click(desktop, "Search actions", "Record Audio")
    assert desktop.get_sim_state()["fileDialogPaused"]
    assert not desktop.get_sim_state()["running"]
    click(desktop, "Save File", "Cancel")
    search(desktop, "zzzz no such setting")
    assert not any(i["label"] == "Game Library" for i in desktop.list_items("Search actions"))
    desktop.send("key enter")
    desktop.wait_frames(3)
    assert desktop.get_dialog_state("GlobalSearch")


@pytest.mark.parametrize("query,label,group", [
    ("VBXE", "VideoBoard XE (VBXE)", "Internal devices"),
    ("vbxe", "VideoBoard XE (VBXE)", "Internal devices"),
    ("blitter", "VideoBoard XE (VBXE)", "Internal devices"),
    ("dragoncart", "DragonCart", "Cartridge devices"),
    ("xf551", "XF551", "Disk drives"),
])
def test_search_device_catalog_and_destination(desktop, query, label, group):
    devices_before = desktop.get_sim_state()["devices"]
    search(desktop, query)
    assert label in desktop.get_item_labels("Search actions")
    click(desktop, "Search actions", label)
    assert desktop.get_dialog_state("SystemConfig")
    assert desktop.query_state()["state"]["configurationPage"] == 12
    assert desktop.get_sim_state()["devices"] == devices_before
    # Search navigates; the existing menu still controls adding hardware.
    click(desktop, "", "Add Device...")
    click(desktop, "", group)
    assert label in desktop.get_item_labels()


def test_search_results_have_left_margin(desktop, tmp_path):
    search(desktop, "vbxe")
    result = next(i for i in desktop.list_items("Search actions")
                  if i["label"] == "VideoBoard XE (VBXE)")
    child = next(w for w in desktop.query_state()["state"]["windows"]
                 if "##results" in w["name"] and "Search actions" in w["name"])
    assert result["x"] > child["x"]
    desktop.send("ui_screenshot " + str(tmp_path / "search-vbxe.png"))


def test_independent_profiles_restore_cpu_memory(desktop, request, monkeypatch, tmp_path):
    emu = desktop
    config(emu, "CPU")
    click(emu, "Configure System", "65C816 (3.58MHz)")
    config(emu, "Memory")
    select_combo(emu, "Configure System", "Memory Size", "128K")
    config(emu, "Devices")
    click(emu, "Configure System", "Add Device...")
    click(emu, "", "HLE devices")
    click(emu, "", "Printer (P:)")
    click(emu, "Printer (P:) Settings", "OK")
    expected = emu.get_sim_state()
    assert "printer" in expected["devices"]
    emu.close_dialog("SystemConfig")
    first = create_profile(emu, "Independent A")
    assert first["categories"] == 0xFFFFFFFF
    assert first["savedCategories"] == 0xFFFFFFFF
    switch_profile(emu, "Independent A")
    config(emu, "CPU")
    click(emu, "Configure System", "6502 / 6502C")
    config(emu, "Memory")
    select_combo(emu, "Configure System", "Memory Size", "64K")
    emu.close_dialog("SystemConfig")
    second = create_profile(emu, "Independent B")
    switch_profile(emu, "Independent B")
    # A was changed before copying B; restore A's original settings and save
    # by switching away, then verify B remains independent.
    switch_profile(emu, "Independent A")
    config(emu, "CPU")
    click(emu, "Configure System", "65C816 (3.58MHz)")
    config(emu, "Memory")
    select_combo(emu, "Configure System", "Memory Size", "128K")
    switch_profile(emu, "Independent B")
    assert emu.get_sim_state()["cpuMode"] != expected["cpuMode"]
    switch_profile(emu, "Independent A")
    for key in ("cpuMode", "cpuMultiplier", "memoryMode", "devices"):
        assert emu.get_sim_state()[key] == expected[key]
    emu.send("quit")
    emu._proc.wait(timeout=5)
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as restarted:
        assert restarted.query_state()["state"]["profiles"]["current"] == first["id"]
        for key in ("cpuMode", "cpuMultiplier", "memoryMode", "devices"):
            assert restarted.get_sim_state()[key] == expected[key]


def test_inherited_categories_editor_cancel_and_cycle_guard(desktop):
    first = create_profile(desktop, "Parent")
    switch_profile(desktop, "Parent")
    click(desktop, "Profiles", "Add inherited profile")
    items = desktop.query_state()["state"]["profiles"]["items"]
    child = next(p for p in items if p["name"] == "New Profile")
    assert child["parent"] == first["id"]
    assert child["categories"] == 0
    click(desktop, "Edit Profile", "Hardware")
    click(desktop, "Edit Profile", "Cancel")
    child = next(p for p in desktop.query_state()["state"]["profiles"]["items"] if p["id"] == child["id"])
    assert child["categories"] == 0
    desktop.send('right_click "Profiles" "New Profile"')
    desktop.wait_frames(3)
    click(desktop, "", "Edit categories and inheritance...")
    click(desktop, "Edit Profile", "Hardware")
    click(desktop, "Edit Profile", "Save profile definition")
    child = next(p for p in desktop.query_state()["state"]["profiles"]["items"] if p["id"] == child["id"])
    assert child["categories"] == 1
    desktop.send('right_click "Profiles" "Parent"')
    desktop.wait_frames(3)
    click(desktop, "", "Edit categories and inheritance...")
    click(desktop, "Edit Profile", "Parent profile")
    assert not any(i["label"] in ("Parent", "New Profile") for i in desktop.list_items() if "##Combo" in i["window"])


def test_shared_game_profile_sessions_and_assignment(desktop, tmp_path, request):
    emu = desktop
    config(emu, "CPU")
    click(emu, "Configure System", "65C816 (3.58MHz)")
    emu.close_dialog("SystemConfig")
    profile = create_profile(emu, "Shared game setup")
    expected_cpu = emu.get_sim_state()["cpuMode"]
    config(emu, "CPU")
    click(emu, "Configure System", "6502 / 6502C")
    emu.close_dialog("SystemConfig")
    emu.close_dialog("Profiles")
    emu.open_dialog("GameLibrary")
    emu.wait_frames(5)
    click(emu, "Game Library", "##row")
    select_combo(emu, "Game Library", "Launch profile", "Shared game setup")
    cache = tmp_path / "config" / "altirra" / "gamelibrary.json"
    assert next(g for g in json.loads(cache.read_text())["games"] if g["displayName"] == "Alpha")["launchProfileId"] == profile["id"]
    click(emu, "Game Library", "Launch")
    emu.wait_frames(8)
    assert emu.query_state()["state"]["profiles"]["temporary"]
    assert emu.get_sim_state()["cpuMode"] == expected_cpu
    config(emu, "CPU")
    click(emu, "Configure System", "6502 / 6502C")
    switch_profile(emu, "Shared game setup")
    assert emu.get_sim_state()["cpuMode"] == expected_cpu
    assert not emu.query_state()["state"]["profiles"]["temporary"]
    emu.close_dialog("Profiles")
    emu.open_dialog("GameLibrary")
    emu.wait_frames(3)
    click(emu, "Game Library", "Launch")
    emu.wait_frames(8)
    config(emu, "CPU")
    click(emu, "Configure System", "6502 / 6502C")
    changed_cpu = emu.get_sim_state()["cpuMode"]
    emu.close_dialog("SystemConfig")
    emu.open_dialog("Profiles")
    emu.wait_frames(3)
    click(emu, "Profiles", "Update saved profile")
    assert emu.query_state()["state"]["profiles"]["temporary"]
    switch_profile(emu, "Shared game setup")
    assert emu.get_sim_state()["cpuMode"] == changed_cpu
    emu.send("quit")
    emu._proc.wait(timeout=5)
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as restarted:
        restarted.close_dialog("SetupWizard")
        restarted.send("set_window_size 1280 900")
        restarted.open_dialog("GameLibrary")
        restarted.wait_frames(10)
        click(restarted, "Game Library", "##row")
        click(restarted, "Game Library", "Launch")
        restarted.wait_frames(8)
        state = restarted.query_state()["state"]
        assert state["profiles"]["current"] == profile["id"]
        assert state["profiles"]["temporary"]
        assert restarted.get_sim_state()["cpuMode"] == changed_cpu


def test_profile_switch_preserves_pause_and_parent_delete_is_guarded(desktop):
    parent = create_profile(desktop, "Parent to keep")
    desktop.send("pause")
    switch_profile(desktop, "Parent to keep")
    assert not desktop.get_sim_state()["running"]
    click(desktop, "Profiles", "Add inherited profile")
    click(desktop, "Edit Profile", "Cancel")
    click(desktop, "Profiles", "Default")
    desktop.send('right_click "Profiles" "Parent to keep"')
    desktop.wait_frames(3)
    delete = next(i for i in desktop.list_items() if i["label"] == "Delete")
    assert delete["disabled"]
    assert any(p["id"] == parent["id"] for p in desktop.query_state()["state"]["profiles"]["items"])


def test_search_trims_query_and_remains_reachable_after_resize(desktop):
    search(desktop, "   Memory Size   ")
    desktop.send("key enter")
    desktop.wait_frames(3)
    assert desktop.get_dialog_state("SystemConfig")
    assert desktop.query_state()["state"]["configurationPage"] == 5
    desktop.close_dialog("SystemConfig")
    search(desktop, "screenshot")
    desktop.send("set_window_size 640 480")
    desktop.wait_frames(5)
    result = next(i for i in desktop.list_items("Search actions") if i["label"] == "Save screenshot")
    assert result["x"] >= 0
    assert result["x"] + result["w"] <= 640
    desktop.send("key escape")
    desktop.wait_frames(3)
    assert not desktop.get_dialog_state("GlobalSearch")


def test_search_does_not_offer_paired_turbo_key_handlers(desktop):
    search(desktop, "warp")
    labels = [i["label"] for i in desktop.list_items("Search actions")]
    # Device matches can put the toggle below the first viewport. Exercise
    # keyboard scrolling and inspect every visible row along the way.
    for _ in range(12):
        desktop.send("key down")
        desktop.wait_frames(3)
        labels.extend(desktop.get_item_labels("Search actions"))
    assert "Toggle Warp Speed" in labels
    assert "Pulse Warp On" not in labels
    assert "Pulse Warp Off" not in labels


def test_missing_assignment_keeps_library_and_pause_state(desktop, tmp_path):
    profile = create_profile(desktop, "Setup to delete")
    desktop.close_dialog("Profiles")
    desktop.open_dialog("GameLibrary")
    desktop.wait_frames(5)
    click(desktop, "Game Library", "##row")
    select_combo(desktop, "Game Library", "Launch profile", "Setup to delete")
    desktop.close_dialog("GameLibrary")
    desktop.open_dialog("Profiles")
    desktop.wait_frames(3)
    desktop.send('right_click "Profiles" "Setup to delete"')
    desktop.wait_frames(3)
    click(desktop, "", "Delete")
    assert not any(p["id"] == profile["id"] for p in desktop.query_state()["state"]["profiles"]["items"])
    desktop.close_dialog("Profiles")
    desktop.send("pause")
    desktop.open_dialog("GameLibrary")
    desktop.wait_frames(3)
    click(desktop, "Game Library", "Launch")
    desktop.wait_frames(5)
    assert desktop.get_dialog_state("GameLibrary")
    assert not desktop.get_sim_state()["running"]
    cache = json.loads((tmp_path / "config" / "altirra" / "gamelibrary.json").read_text())
    assert not any(g.get("playCount", 0) for g in cache["games"])
