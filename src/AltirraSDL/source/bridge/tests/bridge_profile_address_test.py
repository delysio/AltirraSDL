#!/usr/bin/env python3
"""Check profiler address identity through the real bridge and MMU."""

from __future__ import annotations

import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile

from bridge_custom_device_test import start_stderr_reader, wait_for_token_file


def check_addresses(rows: list[dict]) -> None:
    assert rows, "profiler returned no records"
    for row in rows:
        gaddr = int(row["gaddr"][1:], 16)
        assert row["addr"] == f"${gaddr & 0xffff:04x}", row
        assert row["addr24"] == f"${gaddr & 0xffffff:06x}", row
        assert row["gaddr"] == f"${gaddr:08x}", row
    # mmu.cpp: 320K uses PORTB bits 2/3/5/6 for the memory bank,
    # bit 4 clear enables CPU extended RAM, and the global address is
    # kATAddressSpace_PORTB + (normalized PORTB << 16) + PC.
    subroutine_rows = [r for r in rows if r["addr"] == "$4010"]
    addresses = {r["gaddr"] for r in subroutine_rows}
    assert {"$70ef4010", "$70eb4010"} <= addresses, addresses


def main() -> None:
    server = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(Path(sys.argv[2]).resolve()))
    from altirra_bridge import AltirraBridge

    with tempfile.TemporaryDirectory(prefix="altirra profile test ") as td:
        env = os.environ.copy()
        env.update(TMPDIR=td, TEMP=td, TMP=td)
        proc = subprocess.Popen(
            [str(server), "--bridge=tcp:127.0.0.1:0", "--memory=320K"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE, text=True, env=env)
        lines: list[str] = []
        pending: queue.Queue[str] = queue.Queue()
        reader = start_stderr_reader(proc, lines, pending)
        try:
            token_file = wait_for_token_file(proc, pending)
            with AltirraBridge.from_token_file(str(token_file)) as bridge:
                bridge.boot_bare()
                # SEI; JSR $4010; JMP $4000, with an RTS at $4010.
                program = bytes.fromhex("78 20 10 40 4c 00 40")
                for bank in (0xef, 0xeb):
                    bridge.poke(0xd301, bank)
                    bridge.memload(0x4000, program)
                    bridge.poke(0x4010, 0x60)
                bridge.memload(0x060f, bytes.fromhex("4c 00 40"))
                for mode in ("insns", "functions", "basicblock", "callgraph"):
                    bridge.profile_start(mode)
                    for bank in (0xef, 0xeb):
                        bridge.poke(0xd301, bank)
                        bridge.frame(2)
                        bridge.ping()  # Wait for the frame gate to finish.
                    bridge.profile_stop()
                    if mode == "callgraph":
                        rows = bridge.profile_dump_tree()
                        check_addresses(rows)
                        assert bridge.profile_dump_tree() == rows
                    else:
                        report = bridge.profile_dump(top=4096)
                        check_addresses(report["hot"])
                        assert bridge.profile_dump(top=4096) == report
                bridge.quit()
            assert proc.wait(timeout=10) == 0
        except Exception:
            print("\n".join(lines), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            reader.join(timeout=5)
            if proc.stderr:
                proc.stderr.close()
    print("Profiler address identity passed in all four modes")


if __name__ == "__main__":
    main()
