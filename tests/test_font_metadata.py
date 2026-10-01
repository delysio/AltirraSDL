"""Bounded OpenType metadata parsing, independent of SDL and native catalogs."""
import random
import shutil
import struct
import subprocess
from pathlib import Path

import pytest


@pytest.fixture(scope="module")
def reader(tmp_path_factory):
    compiler = shutil.which("c++")
    if not compiler:
        pytest.skip("C++ compiler required")
    directory = tmp_path_factory.mktemp("font-metadata")
    source = directory / "reader.cpp"
    source.write_text('''#include "ui_font_metadata.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(file)), {});
        for (const auto& face : ATUIReadFontMetadata(data.data(), data.size(), true))
            std::cout << face.index << '\\t' << face.monospaced << '\\t'
                      << face.family << '\\t' << face.style << '\\n';
    }
}
''')
    root = Path(__file__).resolve().parents[1] / "src/AltirraSDL/source/ui/core"
    executable = directory / "reader"
    subprocess.run([compiler, "-std=c++17", "-fsanitize=address,undefined", "-g",
                    "-I", str(root), str(source), str(root / "ui_font_metadata.cpp"),
                    "-o", str(executable)], check=True)
    return executable


def sfnt(family, style, base=0, variable=False, mac_roman=False):
    values = [(1, family), (2, style), (16, family), (17, style)]
    if variable:
        values += [(256, "Thin"), (257, "Black")]
    strings = b""
    records = b""
    for name_id, value in values:
        encoded = value.encode("mac_roman" if mac_roman else "utf-16-be")
        records += struct.pack(">6H", 1 if mac_roman else 3, 0 if mac_roman else 1,
                               0 if mac_roman else 0x409, name_id, len(encoded), len(strings))
        strings += encoded
    tables = {b"name": struct.pack(">3H", 0, len(values), 6 + len(records)) + records + strings,
              b"post": struct.pack(">IIhhI", 0x30000, 0, 0, 0, 1),
              b"cmap": b"\0", b"head": b"\0", b"maxp": b"\0"}
    if variable:
        tables[b"fvar"] = (
            struct.pack(">8H", 1, 0, 16, 2, 1, 20, 2, 8)
            + struct.pack(">4siiiHH", b"wght", 100 << 16, 400 << 16, 900 << 16, 0, 258)
            + struct.pack(">HHiHHi", 256, 0, 100 << 16, 257, 0, 900 << 16))
    offset = 12 + len(tables) * 16
    directory = b""
    content = b""
    for tag, data in sorted(tables.items()):
        directory += struct.pack(">4sIII", tag, 0, base + offset, len(data))
        padded = data + b"\0" * (-len(data) % 4)
        content += padded
        offset += len(padded)
    return struct.pack(">I4H", 0x10000, len(tables), 0, 0, 0) + directory + content


def read(reader, tmp_path, data):
    path = tmp_path / "misleading-filename.otf"
    path.write_bytes(data)
    return [line.split("\t") for line in subprocess.check_output(
        [str(reader), str(path)], text=True).splitlines()]


def test_unicode_names_and_named_indices(reader, tmp_path):
    rows = read(reader, tmp_path, sfnt("Žluťoučký 🕹", "Regular", variable=True))
    assert rows == [["0", "1", "Žluťoučký 🕹", "Regular"],
                    ["65536", "1", "Žluťoučký 🕹", "Thin"],
                    ["131072", "1", "Žluťoučký 🕹", "Black"]]


def test_legacy_mac_roman_names(reader, tmp_path):
    assert read(reader, tmp_path, sfnt("École", "Italique", mac_roman=True)) == [
        ["0", "1", "École", "Italique"]]


def test_collection_face_offsets(reader, tmp_path):
    first = sfnt("Review Mono", "Regular", base=20)
    second = sfnt("Review Mono", "Bold Italic", base=20 + len(first))
    data = struct.pack(">4s4I", b"ttcf", 0x10000, 2, 20, 20 + len(first)) + first + second
    assert read(reader, tmp_path, data) == [["0", "1", "Review Mono", "Regular"],
                                         ["1", "1", "Review Mono", "Bold Italic"]]


def test_truncated_and_invalid_offsets(reader, tmp_path):
    complete = sfnt("Review", "Regular", variable=True)
    generator = random.Random(20261001)
    paths = []
    samples = [complete[:n] for n in range(len(complete))]
    samples += [generator.randbytes(n) for n in range(256)]
    damaged = bytearray(complete)
    damaged[20:24] = b"\xff" * 4
    samples += [bytes(damaged), b"ttcf" + b"\xff" * 16]
    for i, data in enumerate(samples):
        path = tmp_path / str(i)
        path.write_bytes(data)
        paths.append(str(path))
    # Sanitizers must remain silent; all malformed data must avoid out-of-bounds reads.
    subprocess.run([str(reader), *paths], check=True, capture_output=True)
