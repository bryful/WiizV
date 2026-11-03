"""Local, headless libretro investigation. Outputs stay in an explicit directory."""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import zlib


class Game(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class Probe:
    def __init__(self, rom: Path, dll: Path, output: Path):
        self.output = output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.dll = C.CDLL(str(dll.resolve()))
        self.core_id = hashlib.sha256(dll.read_bytes()).hexdigest()
        self.rom = rom.read_bytes()
        if len(self.rom) % 32768 == 512:
            self.rom = self.rom[512:]
        self.rom_id = hashlib.sha256(self.rom).hexdigest()
        self.buttons = 0
        self.frame = None
        self.pixel_format = 0
        self.variables = {}
        self.directory = str(self.output).encode()
        self.errors = []

        def environment(command, ptr):
            command &= 0xffff
            if command == 10:
                self.pixel_format = C.cast(ptr, C.POINTER(C.c_int))[0]
                return self.pixel_format in (0, 1, 2)
            if command in (9, 31):
                C.cast(ptr, C.POINTER(C.c_char_p))[0] = self.directory
                return True
            if command == 16:
                entries = C.cast(ptr, C.POINTER(Variable))
                index = 0
                while entries[index].key:
                    text = entries[index].value or b""
                    self.variables[entries[index].key] = text.split(b"; ", 1)[-1].split(b"|", 1)[0]
                    index += 1
                return True
            if command == 15:
                variable = C.cast(ptr, C.POINTER(Variable))
                variable[0].value = self.variables.get(variable[0].key)
                return variable[0].value is not None
            if command in (17, 3):
                C.cast(ptr, C.POINTER(C.c_bool))[0] = command == 3
                return True
            if command == 52:
                C.cast(ptr, C.POINTER(C.c_uint))[0] = 0
                return True
            return command in (11, 35, 36, 37, 42)

        def video(data, width, height, pitch):
            if data and data != C.c_void_p(-1).value:
                self.frame = (width, height, pitch, C.string_at(data, pitch * height))

        self.callbacks = [
            C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)(environment),
            C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)(video),
            C.CFUNCTYPE(None, C.c_int16, C.c_int16)(lambda *_: None),
            C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)(lambda _, count: count),
            C.CFUNCTYPE(None)(lambda: None),
            C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)(
                lambda port, device, index, button: (self.buttons >> button) & 1 if port == 0 and device == 1 and button < 16 else 0),
        ]
        for name, callback in zip(("environment", "video_refresh", "audio_sample", "audio_sample_batch", "input_poll", "input_state"), self.callbacks):
            getattr(self.dll, "retro_set_" + name)(callback)
        self.dll.retro_init()
        self.rom_buffer = C.create_string_buffer(self.rom)
        self.rom_path = str(rom.resolve()).encode()
        game = Game(self.rom_path, C.cast(self.rom_buffer, C.c_void_p), len(self.rom), None)
        self.dll.retro_load_game.argtypes = [C.POINTER(Game)]
        self.dll.retro_load_game.restype = C.c_bool
        if not self.dll.retro_load_game(C.byref(game)):
            raise RuntimeError("ROM load failed")
        self.dll.retro_get_memory_data.restype = C.c_void_p
        self.dll.retro_get_memory_size.restype = C.c_size_t
        self.dll.retro_serialize_size.restype = C.c_size_t
        self.dll.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        self.dll.retro_serialize.restype = C.c_bool
        self.dll.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        self.dll.retro_unserialize.restype = C.c_bool
        self.ram = (C.c_uint8 * self.dll.retro_get_memory_size(2)).from_address(self.dll.retro_get_memory_data(2))
        self.dll.retro_set_controller_port_device(0, 1)
        self.dll.retro_set_controller_port_device(1, 0)

    def run(self, frames, buttons=0):
        self.buttons = buttons
        for _ in range(frames):
            self.dll.retro_run()

    def state(self):
        buffer = C.create_string_buffer(self.dll.retro_serialize_size())
        if not self.dll.retro_serialize(buffer, len(buffer)):
            raise RuntimeError("serialize failed")
        payload = buffer.raw
        header = ("WIIZVST1" + self.rom_id + self.core_id + hashlib.sha256(payload).hexdigest()).encode()
        return header + struct.pack("<Q", len(payload)) + payload

    def load(self, path):
        payload = Path(path).read_bytes()
        if payload.startswith(b"WIIZVST1"):
            payload = payload[208:]
        buffer = C.create_string_buffer(payload)
        if not self.dll.retro_unserialize(buffer, len(payload)):
            raise RuntimeError("unserialize failed")

    def snapshot(self, label):
        (self.output / (label + ".ram")).write_bytes(bytes(self.ram))
        (self.output / (label + ".wvs")).write_bytes(self.state())
        if self.frame:
            width, height, pitch, pixels = self.frame
            rgb = bytearray()
            for y in range(height):
                rgb.append(0)
                for x in range(width):
                    if self.pixel_format == 1:
                        b, g, r = pixels[y * pitch + x * 4:y * pitch + x * 4 + 3]
                    else:
                        value = struct.unpack_from("<H", pixels, y * pitch + x * 2)[0]
                        if self.pixel_format == 2:
                            r, g, b = ((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31
                        else:
                            r, g, b = ((value >> 10) & 31) * 255 // 31, ((value >> 5) & 31) * 255 // 31, (value & 31) * 255 // 31
                    rgb.extend((r, g, b))
            def chunk(name, data):
                return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
            png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            png += chunk(b"IDAT", zlib.compress(rgb)) + chunk(b"IEND", b"")
            (self.output / (label + ".png")).write_bytes(png)
        print(label, "cursor", self.ram[0x33], "pos", bytes(self.ram[0xb0:0xc0]).hex(), "party", bytes(self.ram[0x70:0x90]).hex(), flush=True)

    def close(self):
        self.dll.retro_unload_game()
        self.dll.retro_deinit()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=Path("x64/Release/wiizv_snes9x.dll"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--commands", type=Path, required=True)
    parser.add_argument("--state", type=Path)
    args = parser.parse_args()
    probe = Probe(args.rom, args.core, args.output)
    try:
        if args.state:
            probe.load(args.state)
        for step in json.loads(args.commands.read_text(encoding="utf-8-sig")):
            if "poke" in step:
                for offset, value in step["poke"].items():
                    probe.ram[int(offset, 0)] = value
            probe.run(step.get("frames", 0), step.get("buttons", 0))
            if label := step.get("label"):
                probe.snapshot(label)
    finally:
        probe.close()


if __name__ == "__main__":
    main()
