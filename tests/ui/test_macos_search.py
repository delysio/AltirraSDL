"""Native Help/Search integration; exercised by a macOS UI test runner."""
import sys

import pytest

from .test_desktop_search_profiles import desktop


pytestmark = pytest.mark.skipif(sys.platform != "darwin", reason="Native macOS menu test")


def test_native_help_search_binding_and_activation(desktop):
    state = desktop.send("native_search_menu")
    assert state["present"]  # Also verifies registration as NSApplication.helpMenu.
    assert state["key"] == ord("k")
    assert state["control"]
    assert not state["shift"] and not state["option"]
    # macOS has no duplicate ImGui corner input or menu bar.
    assert not desktop.list_items("##MainMenuBar")
    desktop.send("native_search_activate")
    desktop.wait_frames(5)
    assert desktop.get_dialog_state("GlobalSearch")
    assert "##search" in desktop.get_item_labels("Search actions")
