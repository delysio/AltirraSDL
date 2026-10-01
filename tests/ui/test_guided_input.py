"""Guided maps use normal persistence and physical SDL capture/routing."""
import pytest

from .harness import AltirraTestHarness
from .test_explorers import click

WINDOW = "Guided Joystick Setup"
MAP = "Guided setup joystick (Port {})"
# Canonical ATInputCode / ATInputTrigger values from inputdefs.h.
TARGETS = [0x100, 0x101, 0x102, 0x103, 0]


@pytest.fixture
def guided(request, monkeypatch, tmp_path):
    # A dummy SDL window prevents desktop focus changes from cancelling
    # capture while still exercising rendered ImGui and SDL device events.
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    monkeypatch.setenv("SDL_AUDIODRIVER", "dummy")
    monkeypatch.setenv("ALTIRRA_DISPLAY_BACKEND", "sdlrenderer")
    (tmp_path / "config").mkdir()
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    with AltirraTestHarness(executable=request.config.getoption("--emu-path")) as emu:
        emu.close_dialog("SetupWizard")
        emu.send("set_window_size 1280 900")
        emu.wait_frames(8)
        yield emu


def open_setup(emu, port=1):
    emu.open_dialog("InputMappings")
    emu.wait_frames(5)
    click(emu, "Input Mappings", "Guided Joystick Setup...")
    if port == 2:
        click(emu, WINDOW, "Joystick 2 - Port 2")


def physical_key(emu, name, down):
    emu.send(f"physical_key {'down' if down else 'up'} {name}")
    emu.wait_frames(4)


def configure_keyboard(emu, names=("Up", "Down", "Left", "Right", "F2")):
    click(emu, WINDOW, "Configure all")
    for name in names:
        physical_key(emu, name, True)
        assert emu.send("input_maps")["capturing"]  # waits for release
        if name == "F2":
            assert emu.mem_read(0xD01F)[0] == 7  # capture must not press Atari START
        physical_key(emu, name, False)
    assert not emu.send("input_maps")["capturing"]


def map_for(emu, port=1):
    matches = [m for m in emu.send("input_maps")["maps"]
               if m["name"] == MAP.format(port)]
    assert len(matches) == 1
    return matches[0]


def apply(emu, disable_others=False):
    click(emu, WINDOW, "Review and apply...")
    if disable_others:
        labels = {i["label"] for i in emu.list_items(WINDOW)}
        for m in emu.send("input_maps")["maps"]:
            if m["enabled"] and m["name"] in labels:
                click(emu, WINDOW, m["name"])
    click(emu, WINDOW, "Apply")


def close_setup(emu):
    click(emu, WINDOW, "Close")
    emu.close_dialog("InputMappings")
    emu.wait_frames(6)


def row_click(emu, label, row):
    items = [i for i in emu.list_items(WINDOW) if i["label"] == label]
    item = items[row]
    emu.send(f"click_at {item['x'] + item['w'] / 2} {item['y'] + item['h'] / 2}")
    emu.wait_frames(6)


def test_keyboard_reuses_map_and_preserves_advanced_bindings(guided):
    emu = guided
    existing = emu.send("input_maps")["maps"]
    open_setup(emu)
    configure_keyboard(emu)
    apply(emu)
    original = map_for(emu)
    assert [m for m in emu.send("input_maps")["maps"]
            if m["name"] != MAP.format(1)] == existing
    assert [b["target"] for b in original["bindings"]] == TARGETS
    assert [b["input"] for b in original["bindings"]] == [38, 40, 37, 39, 113]
    close_setup(emu)
    index = next(i for i, m in enumerate(emu.send("input_maps")["maps"])
                 if m["name"] == MAP.format(1))
    # Advanced editor equivalents: F9 autofire and conditional F8 Up.
    emu.send(f"input_map_add_binding {index} 0 120 0x10000")
    emu.send(f"input_map_add_binding {index} 0 0x10077 0x100")
    open_setup(emu)
    row_click(emu, "Add binding", 4)
    physical_key(emu, "Space", True)
    physical_key(emu, "Space", False)
    row_click(emu, "Wait for input", 0)
    physical_key(emu, "W", True)
    physical_key(emu, "W", False)
    apply(emu)
    updated = map_for(emu)
    assert [m for m in emu.send("input_maps")["maps"]
            if m["name"] != MAP.format(1)] == existing
    assert len(updated["bindings"]) == 8
    assert {b["input"] for b in updated["bindings"] if b["target"] == 0} == {113, 32}
    assert {"input": 120, "target": 0x10000, "port": 0} in updated["bindings"]
    assert {"input": 0x10077, "target": 0x100, "port": 0} in updated["bindings"]
    assert {"input": 87, "target": 0x100, "port": 0} in updated["bindings"]
    close_setup(emu)
    # Restart in the same isolated settings directory; still one map.
    emu.stop()
    emu.start()
    emu.wait_frames(8)
    assert map_for(emu) == updated


def test_keyboard_edit_preserves_disconnected_controller(guided):
    emu = guided
    device = emu.send("virtual_controller attach raw")["index"]
    emu.wait_frames(8)
    open_setup(emu)
    configure_keyboard(emu)
    click(emu, WINDOW, "Controller")
    row_click(emu, "Add binding", 4)
    controller_input(emu, device, "button", 0, 1)
    controller_input(emu, device, "button", 0, 0)
    apply(emu)
    original = map_for(emu)
    close_setup(emu)
    emu.send(f"virtual_controller detach {device}")
    emu.wait_frames(8)
    open_setup(emu)
    click(emu, WINDOW, "Keyboard")
    row_click(emu, "Wait for input", 0)
    physical_key(emu, "W", True)
    physical_key(emu, "W", False)
    apply(emu)
    updated = map_for(emu)
    assert updated["unit"] == original["unit"]
    assert {"input": 0x2800, "target": 0, "port": 0} in updated["bindings"]
    assert {"input": 87, "target": 0x100, "port": 0} in updated["bindings"]
    close_setup(emu)


def test_review_disables_other_maps_without_replacing_them(guided):
    emu = guided
    existing = emu.send("input_maps")["maps"]
    open_setup(emu)
    configure_keyboard(emu)
    apply(emu, disable_others=True)
    saved = {m["name"]: m for m in emu.send("input_maps")["maps"]}
    disabled = 0
    for before in existing:
        after = saved[before["name"]]
        assert after["bindings"] == before["bindings"]
        assert after["unit"] == before["unit"]
        if before["enabled"] and not after["enabled"]:
            disabled += 1
    assert disabled > 0
    assert map_for(emu)["enabled"]
    close_setup(emu)


def test_capture_cancel_and_closing_discard_changes(guided):
    emu = guided
    open_setup(emu)
    click(emu, WINDOW, "Configure all")
    physical_key(emu, "Left Shift", True)
    physical_key(emu, "Escape", True)
    assert not emu.send("input_maps")["capturing"]
    physical_key(emu, "Escape", False)
    physical_key(emu, "Left Shift", False)
    assert emu.send("query_window_label " + WINDOW)["visible"]
    row_click(emu, "Wait for input", 0)
    # Pause is accepted through the shared gameplay scancode translation.
    physical_key(emu, "Pause", True)
    physical_key(emu, "Pause", False)
    assert not emu.send("input_maps")["capturing"]
    click(emu, WINDOW, "Configure all")
    physical_key(emu, "A", True)
    click(emu, WINDOW, "Close")
    physical_key(emu, "A", False)
    assert not emu.send("input_maps")["capturing"]
    assert not any(m["name"] == MAP.format(1) for m in emu.send("input_maps")["maps"])


def assert_register(emu, address, expected):
    for _ in range(30):
        value = emu.mem_read(address)[0]
        if value == expected:
            return
        emu.wait_frames(3)
    assert value == expected, (emu.get_sim_state(), emu.send("input_maps"))


def controller_input(emu, device, kind, index, value):
    emu.send(f"virtual_controller {kind} {device} {index} {value}")
    emu.wait_frames(5)


@pytest.mark.parametrize("kind", ["raw", "gamepad"])
def test_two_controllers_capture_and_route_to_separate_ports(guided, kind):
    emu = guided
    first = emu.send("virtual_controller attach " + kind)["index"]
    second = emu.send("virtual_controller attach " + kind)["index"]
    emu.wait_frames(10)
    units = []
    for port, device, other in [(1, first, second), (2, second, first)]:
        open_setup(emu, port)
        click(emu, WINDOW, "Controller")
        click(emu, WINDOW, "Configure all")
        # Stick up/down/left/right, then fire. First input identifies device.
        controller_input(emu, device, "axis", 1, -32767)
        assert emu.send("input_maps")["capturing"]
        controller_input(emu, device, "axis", 1, 0)
        # The other device cannot fill the next mapping.
        controller_input(emu, other, "button", 0, 1)
        controller_input(emu, other, "button", 0, 0)
        assert emu.send("input_maps")["capturing"]
        for axis, value in [(1, 32767), (0, -32767), (0, 32767)]:
            controller_input(emu, device, "axis", axis, value)
            controller_input(emu, device, "axis", axis, 0)
        controller_input(emu, device, "button", 0, 1)
        controller_input(emu, device, "button", 0, 0)
        assert not emu.send("input_maps")["capturing"]
        if kind == "gamepad":
            # D-pad Up and R2/RT can coexist with stick Up and face-button Fire.
            row_click(emu, "Add binding", 0)
            controller_input(emu, device, "button", 11, 1)  # SDL_GAMEPAD_BUTTON_DPAD_UP
            controller_input(emu, device, "button", 11, 0)
            row_click(emu, "Add binding", 4)
            controller_input(emu, device, "axis", 5, 32767)  # right trigger
            controller_input(emu, device, "axis", 5, -32768)
        # Keyboard bindings can be added to a device-specific controller map.
        click(emu, WINDOW, "Keyboard")
        row_click(emu, "Add binding", 4)
        physical_key(emu, "Space", True)
        physical_key(emu, "Space", False)
        assert not emu.send("input_maps")["capturing"]
        apply(emu, disable_others=(port == 1))
        m = map_for(emu, port)
        units.append(m["unit"])
        expected = [0x2102, 0x2103, 0x2100, 0x2101, 0x2800]
        if kind == "gamepad":
            expected = [0x2102, 0x210E, 0x2103, 0x2100, 0x2101, 0x2800, 0x210B]
        expected.append(32)
        assert [b["input"] for b in m["bindings"]] == expected
        assert all(b["port"] == port - 1 for b in m["bindings"])
        close_setup(emu)
    assert units[0] != units[1]
    # Verify actual PIA port routing, beyond serialized map contents.
    emu.wait_frames(120)
    assert_register(emu, 0xD300, 0xFF)
    controller_input(emu, first, "axis", 1, -32767)
    assert_register(emu, 0xD300, 0xFE)
    controller_input(emu, first, "axis", 1, 0)
    controller_input(emu, second, "axis", 1, -32767)
    assert_register(emu, 0xD300, 0xEF)
    controller_input(emu, second, "axis", 1, 0)
    controller_input(emu, first, "button", 0, 1)
    assert_register(emu, 0xD010, 0)
    assert_register(emu, 0xD011, 1)
    controller_input(emu, first, "button", 0, 0)
    assert_register(emu, 0xD010, 1)

    if kind == "gamepad":
        controller_input(emu, second, "axis", 5, 32767)
        assert_register(emu, 0xD011, 0)
        controller_input(emu, second, "axis", 5, -32768)
        assert_register(emu, 0xD011, 1)
        controller_input(emu, second, "button", 11, 1)
        assert_register(emu, 0xD300, 0xEF)
        controller_input(emu, second, "button", 11, 0)
        assert_register(emu, 0xD300, 0xFF)


def test_held_controller_is_released_and_cannot_leak_on_cancel(guided):
    emu = guided
    device = emu.send("virtual_controller attach raw")["index"]
    emu.wait_frames(120)
    controller_input(emu, device, "axis", 1, -32767)
    assert_register(emu, 0xD300, 0xFE)
    open_setup(emu)
    assert_register(emu, 0xD300, 0xFF)
    click(emu, WINDOW, "Controller")
    click(emu, WINDOW, "Configure all")
    # Initially-held input cannot satisfy the first capture.
    assert emu.send("input_maps")["capturing"]
    close_setup(emu)
    assert_register(emu, 0xD300, 0xFF)
    controller_input(emu, device, "axis", 1, 0)
    controller_input(emu, device, "axis", 1, -32767)
    assert_register(emu, 0xD300, 0xFE)
    controller_input(emu, device, "axis", 1, 0)
    assert_register(emu, 0xD300, 0xFF)


def test_disconnect_cancels_capture(guided):
    emu = guided
    device = emu.send("virtual_controller attach raw")["index"]
    emu.wait_frames(8)
    open_setup(emu)
    click(emu, WINDOW, "Controller")
    click(emu, WINDOW, "Configure all")
    controller_input(emu, device, "hat", 0, 1)  # SDL_HAT_UP
    controller_input(emu, device, "hat", 0, 0)
    assert emu.send("input_maps")["capturing"]
    emu.send(f"virtual_controller detach {device}")
    emu.wait_frames(8)
    assert not emu.send("input_maps")["capturing"]
    close_setup(emu)


def test_focus_loss_cancels_without_stuck_keys(guided):
    emu = guided
    open_setup(emu)
    click(emu, WINDOW, "Configure all")
    physical_key(emu, "A", True)
    emu.send("physical_focus lost")
    emu.wait_frames(5)
    assert not emu.send("input_maps")["capturing"]
    emu.send("physical_focus gained")
    emu.wait_frames(5)
    row_click(emu, "Wait for input", 0)
    physical_key(emu, "B", True)
    physical_key(emu, "B", False)
    assert not emu.send("input_maps")["capturing"]
    close_setup(emu)


def test_resize_keeps_guided_dialog_on_canvas(guided):
    emu = guided
    open_setup(emu)
    for width, height in [(390, 800), (1280, 720)]:
        emu.send(f"set_window_size {width} {height}")
        emu.wait_frames(8)
        window = emu.send("query_window_label " + WINDOW)
        assert window["visible"]
        assert window["x"] >= 0 and window["y"] >= 0
        assert window["x"] + window["w"] <= width
        assert window["y"] + window["h"] <= height
    close_setup(emu)
