"""65816 ROM inspection; opcode facts sourced from ares' WDC65816 table.
https://github.com/ares-emulator/ares/blob/master/ares/component/processor/wdc65816/disassembler.cpp
This is a linear inspection helper: branch-dependent M/X flags need manual review.
"""
import argparse
import json
from pathlib import Path
import re


def disassemble(rom, start, end, m=True, x=False):
    table = json.loads(Path(__file__).with_name("65816_opcodes.json").read_text())
    pc = start
    while pc < end:
        offset = ((pc >> 16) & 0x7f) * 0x8000 + (pc & 0x7fff)
        opcode = rom[offset]
        name, mode = table[f"{opcode:02x}"]
        if mode == "implied": size = 0
        elif mode == "immediateA": size = 1 if m else 2
        elif mode == "immediateX": size = 1 if x else 2
        elif mode.startswith("absoluteLong"): size = 3
        elif mode.startswith("absolute") or mode in ("indirectPC", "indirectLongPC", "indirectX", "relativeWord", "move"): size = 2
        else: size = 1
        data = rom[offset + 1:offset + 1 + size]
        value = int.from_bytes(data, "little")
        operand = f"${value:0{size*2}x}" if size else ""
        if mode.startswith("immediate"): operand = "#" + operand
        if mode in ("relative", "relativeWord"):
            operand = f"${(pc + size + 1 + int.from_bytes(data, 'little', signed=True)) & 0xffffff:06x}"
        if mode in ("directX", "absoluteX", "absoluteLongX"): operand += ",x"
        if mode in ("directY", "absoluteY"): operand += ",y"
        if mode in ("indirect", "indirectPC"): operand = "(" + operand + ")"
        if mode == "indexedIndirectX" or mode == "indirectX": operand = "(" + operand + ",x)"
        if mode == "indirectIndexedY": operand = "(" + operand + "),y"
        if mode in ("indirectLong", "indirectLongPC"): operand = "[" + operand + "]"
        if mode == "indirectLongY": operand = "[" + operand + "],y"
        print(f"{pc:06x}  {rom[offset:offset+size+1].hex(' '):12} {name:3} {operand}")
        if opcode == 0xc2: m, x = m and not (value & 0x20), x and not (value & 0x10)
        if opcode == 0xe2: m, x = m or bool(value & 0x20), x or bool(value & 0x10)
        pc += size + 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--start", default="058000")
    parser.add_argument("--end", default="058100")
    parser.add_argument("--m16", action="store_true")
    parser.add_argument("--x8", action="store_true")
    parser.add_argument("--import-table", type=Path)
    args = parser.parse_args()
    if args.import_table:
        entries = re.findall(r'op\(0x([0-9a-f]{2}), "([^"]+)", (\w+)\)', args.import_table.read_text())
        assert len(entries) == 256
        table = {a: [b, c] for a, b, c in entries}
        table["74"] = ["stz", "directX"]
        Path(__file__).with_name("65816_opcodes.json").write_text(json.dumps(table, indent=2))
    else:
        disassemble(args.rom.read_bytes(), int(args.start.replace(":", ""), 16), int(args.end.replace(":", ""), 16), not args.m16, args.x8)
