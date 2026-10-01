"""Generate the settings search catalogue from existing configuration widgets.

Run after adding pages or controls. CMake also regenerates the catalogue on
changes, so labels and destinations stay synchronized with the actual UI.
"""
import argparse
import json
from pathlib import Path
import re


def generate(source):
    dispatcher = (source / "ui/sysconfig/ui_system.cpp").read_text()
    enum = re.search(r"enum\s*\{(.*?)\};", dispatcher, re.S)[1]
    names = re.findall(r"\b(kCat_\w+)\s*[,=]", enum)
    ids = {name: i for i, name in enumerate(names)}
    functions = dict(re.findall(r"case (kCat_\w+):\s*(Render\w+Category)\(", dispatcher))
    pages = []
    group = ""
    for label, category in re.findall(r'\{\s*"([^"]+)",\s*(-1|kCat_\w+),\s*\d\s*\}', dispatcher):
        if category == "-1":
            group = label
        else:
            pages.append((category, f"{group} > {label}" if group else label))
    aliases = {
        "System": "computer machine model Atari 800 XL XE XEGS 5200",
        "CPU": "processor 6502 65C02 65C816 clock multiplier",
        "Memory": "RAM expansion size banks",
        "Input": "controller gamepad joystick mouse controls",
        "Keyboard": "keys typing keyboard mapping",
        "Firmware": "ROM BIOS operating system BASIC",
        "Video": "PAL NTSC SECAM standard picture",
        "Display": "screen too small scale scaling stretch filter zoom",
        "Audio": "sound volume mute stereo POKEY",
        "Speed": "fast slow turbo warp pacing",
        "EaseOfUse": "rewind pause inactive background",
    }
    sources = "\n".join(p.read_text() for folder in ("ui/sysconfig", "ui/firmware", "ui/core")
                        for p in sorted((source / folder).glob("*.cpp")))
    rows = []
    # Device menu labels are data-driven, not literal ImGui calls. Index the
    # same catalog used by Add Device, including tags and descriptions, and
    # lead the user to the existing Add Device control without adding hardware.
    device_source = (source / "ui/sysconfig/ui_system_pages_b.cpp").read_text()
    literal = r'"((?:[^"\\]|\\.)*)"'
    device_arrays = dict(re.findall(
        r'static const DeviceCatalogEntry (\w+)\[\]\s*=\s*\{(.*?)\n\};',
        device_source, re.S))
    device_groups = re.findall(
        r'\{\s*' + literal + r'\s*,\s*(k\w+Devices)\s*,', device_source)

    def decode(text):
        return json.loads('"' + text + '"')

    for category, path in pages:
        keywords = aliases.get(category.removeprefix("kCat_"), "")
        rows.append((ids[category], path.split(" > ")[-1], path, "", keywords))
        if category == "kCat_Devices":
            for group, array in device_groups:
                for tag, label, description in re.findall(
                        r'\{\s*' + literal + r'\s*,\s*' + literal + r'\s*,\s*' + literal,
                        device_arrays.get(array, "")):
                    rows.append((ids[category], decode(label), path + " > " + decode(group),
                                 "Add Device...", decode(tag) + " " + decode(description)))
        fn = functions.get(category)
        match = re.search(r"void " + (fn or "_missing_") + r"\([^)]*\)\s*\{", sources)
        if not match:
            continue
        # Balance the actual function body, ignoring braces in comments and
        # string/character literals. Never leak controls from the next function.
        remainder = sources[match.end():]
        tokens = re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', remainder, re.S)
        depth = 1
        for token in tokens:
            if token[0] == "{":
                depth += 1
            elif token[0] == "}":
                depth -= 1
                if depth == 0:
                    body = remainder[:token.start()]
                    break
        else:
            raise ValueError(f"Unterminated configuration function: {fn}")
        seen = set()
        widgets = re.finditer(r'ImGui::(?:Checkbox|RadioButton|Combo|BeginCombo|Slider\w+|Drag\w+|Input\w+|ColorEdit\w+|Button)\(\s*"((?:[^"\\]|\\.)*)"', body)
        for widget in widgets:
            raw = widget[1]
            label = raw.split("##")[0]
            if not label:
                preceding = body[max(0, widget.start() - 650):widget.start()]
                labels = re.findall(r'ImGui::(?:Text|TextUnformatted|SeparatorText)\("([^"%]+)"', preceding)
                label = labels[-1].rstrip(": ") if labels else ""
            if not label or label in seen or '%' in label or '\\' in label:
                continue
            seen.add(label)
            next_widget = re.search(r"ImGui::(?:Checkbox|RadioButton|Combo|BeginCombo|Slider|Drag|Input|ColorEdit|Button)", body[widget.end():])
            tip_end = widget.end() + next_widget.start() if next_widget else len(body)
            following = body[widget.end():tip_end]
            tip = re.search(r'SetItemTooltip\("([^"%]+)"', following)
            rows.append((ids[category], label, path, raw, keywords + " " + (tip[1] if tip else "")))
    lines = ["// Generated by tools/generate_ui_search.py. Do not edit by hand."]
    for category, label, path, widget, keywords in rows:
        fields = ", ".join(json.dumps(x, ensure_ascii=False) for x in (label, path, widget, keywords))
        lines.append(f"\t{{{category}, {fields}}},")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    content = generate(args.source)
    if not args.output.exists() or args.output.read_text() != content:
        args.output.write_text(content)
    else:
        args.output.touch()
