# Desktop search and reusable game profiles

## Search

Choose **Search actions and settings…** at the right of the menu bar,
choose it from **Help**, or press **Ctrl+K**. The shortcut can be changed in
Keyboard Shortcuts. Desktop menus wrap when their labels exceed the window
width. On macOS, Search is available in the native Help menu, with the
configured shortcut; no duplicate ImGui menu bar is displayed.

Type an action, setting, or related term such as `controller`, `memory`,
`PAL`, or `screenshot`. Use Up/Down and Enter, or click a result. Settings
open their existing Configure System page and briefly highlight the control.
Unavailable actions remain visible but cannot be executed.

Search reads registered commands and a settings catalogue generated from
configuration page source files during the build. Newly registered commands
are included automatically; literal configuration controls are indexed with
their category and tooltip. Search also indexes the complete Add Device catalog:
device names, tags, and descriptions, with their device group in the result path.
For example, `VBXE` and `vbxe` both find **VideoBoard XE (VBXE)** under
**Peripherals > Devices > Internal devices**. Opening a device result highlights
**Add Device...** on the Devices page; it does not attach or change a device.
Choose the indicated group from that menu to add it through the normal workflow.
Other dynamically generated controls can be found via their category rather
than every possible value. Search is case-insensitive.

Results use an inset content margin. The title and description share the same
text origin, so the expanded selection rectangle does not clip initial letters.

Search verification (2026-10-01): the Linux review build passed, and all 10
`test_search` UI cases passed. These cover VBXE in both cases, description
keywords, devices in other groups, navigation without attaching hardware,
left margins, action activation, no matches, resizing, and keyboard scrolling.
The VBXE result screenshot was also visually inspected. This verifies the
current catalog and tested paths; it does not establish indexing of every
dynamic value or every device configuration dialog.

## Fonts and search appearance

Open **Configure System → Emulator → Fonts**, or **Debug → Change Font**.
The UI and debugger monospace fonts have separate family, style, and size
selections. Family filters match UTF-8 names without case sensitivity.
**Refresh font list** discovers fonts installed while Altirra is running.
Style lists include italic faces and use the installed font's metadata.
**Reset to Defaults** restores Roboto Medium 11 pt and Fira Mono Regular 10 pt.

Linux builds use Fontconfig when its headers were available at build time
and its library is available at runtime. It supplies the installed file catalog.
Font family/style names and collection face indices are read directly from
OpenType metadata, including Unicode and Mac Roman names. Directory discovery
supports TTF, OTF, TTC, and OTC files, including user fonts under XDG_DATA_HOME.
Embedded Roboto and Fira Mono remain available in either case.

Optional FreeType support renders variable fonts' named styles with their
variation coordinates. The library is loaded at runtime; when unavailable,
the built-in rasterizer remains available and named variable instances are
excluded. Default compatible faces remain selectable. Both optional backends
can be disabled with ALTIRRA_ENABLE_FONTCONFIG and ALTIRRA_ENABLE_FREETYPE.

Changes apply live and persist in `settings.ini`, including Unicode family
names. Missing families use the bundled font appropriate to the UI or
monospace role. Search uses larger headings, stronger result titles where
the selected family provides a bold face, smaller descriptions, and spacing
that follows the chosen size.

Verification (2026-10-01): the build with the optional font backends enabled
completed successfully. The latest executed font suite passed 18 of 19 cases,
including menu wrapping and Help/Search activation at 32 pt in 640- and
1280-pixel windows, live sizing, restart persistence, collection refresh,
Unicode names, and operation without Fontconfig. Screenshots of both wrapped
menu sizes were inspected. The variable-style test could not reach Black in
a scrolled style popup; the picker now allows 16 visible styles, but this final
adjustment has not been rebuilt or retested at the user's request.

Four standalone font metadata cases passed with AddressSanitizer and
UndefinedBehaviorSanitizer, covering Unicode, Mac Roman, collection offsets,
truncation, and malformed data. The earlier combined run passed all 31 font,
search, and profile cases. Its reviewed search screenshots cover Roboto,
Liberation Serif Bold Italic, and JetBrains Mono at 8, 18, and 32 pt.

A build with both optional backends disabled was interrupted before completion
when the user requested finalization. New directory-collection and missing
FreeType runtime cases have been added but not executed. Native macOS Help
menu registration, action dispatch, and shortcut refresh are implemented;
the macOS-only UI test has not run on this Linux host. These checks remain
outstanding rather than verified.

Run tests/ui/test_fonts.py, tests/ui/test_desktop_search_profiles.py, and
(on macOS) tests/ui/test_macos_search.py with pytest and --emu-path pointing
to the executable. The collection test requires fonttools; Linux runtime
failure injection requires a C compiler. Standalone metadata tests are in
tests/test_font_metadata.py and require C++ sanitizer runtimes.
Reviewed screenshots and logs are retained under
build/explorer-review/font-review/.

## Profiles

Open **System → Profiles**.

- **Add Profile** captures an independent copy of the live configuration,
  including every native settings category: hardware, firmware selections,
  devices, acceleration, input maps, display, audio, and device NVRAM.
- **Add inherited profile** creates a profile sharing its parent's settings.
- **Edit active profile**, or the profile's context menu, lets you select its
  parent and which categories it owns. Unselected categories are inherited.
  Parent choices exclude descendants to prevent inheritance cycles. A profile
  used as a parent cannot be deleted until its children are reassigned.
- Editing an active definition reloads the profile and resets the machine;
  Cancel leaves the definition unchanged.

Profiles store configuration and media/firmware references. They do not embed
ROM files or replace save states. In ordinary profile editing, changes to an
inherited category are saved to the ancestor that owns it.

## Assigning profiles to games

In Game Library, select a game and choose its **Launch profile**. Ctrl-click
multiple games to assign the same shared profile to all of them. Choose
**Current configuration** to launch without switching profiles.

Assignments survive restarts and library rescans and are also used when
launching those entries from Gaming Mode. A missing assigned profile produces
an error; choose an available profile in Game Library to repair it.

An assigned profile runs as a temporary session. Experiments do not overwrite
its saved settings. In Profiles, choose **Update saved profile** to save the
categories owned by that profile. Inherited ancestors remain unchanged.
Selecting a profile normally exits the temporary session and reloads its
saved configuration, discarding unsaved experiments. Profile switching preserves
whether the emulator is paused.

Game launches supply their selected media, so assigned profiles do not restore
unrelated mounted media or change fullscreen state. Explicit game-session updates
also leave those two saved categories untouched. Updates should be made
with awareness that all games assigned to the profile share the result.
