"""対象ROMと探索中のステートを使用する、宝箱解除失敗の回帰検証。"""
import argparse
import ctypes as C
from pathlib import Path
from snes_probe import Probe


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--core', type=Path, default=Path('x64/Release/wiizv_snes9x.dll'))
    parser.add_argument('--output', type=Path, default=Path('verification/traps'))
    args = parser.parse_args()
    p = Probe(args.rom, args.core, args.output)
    try:
        def run(frames, buttons=0):
            for _ in range(frames):
                assert p.dll.wiizv_set_effects(mask)
                p.run(1, buttons)
                assert p.dll.wiizv_set_effects(mask)

        def tap(button):
            run(2, button)
            run(90)

        for numerator, denominator in [(1, 1), (3, 2), (2, 1), (3, 1), (4, 1)]:
            for mask in (0, 31):
                for trap in (2, 8):
                    p.dll.wiizv_set_audio_speed(1, 1)
                    p.dll.wiizv_set_effects(0)
                    p.load(args.state)
                    p.run(1)
                    assert 1 <= p.ram[0xb7] <= 8 and p.ram[0xdb] == 6
                    assert p.ram[0x1a] == 0xd8 and p.ram[0x1b] == 0x1f
                    # NMIのコルーチン復帰先を宝箱メニューへ置換する。ROMは変更しない。
                    state = bytearray(p.state()[208:])
                    registers = state.index(b'REG:000016:') + 11
                    state[registers:registers + 16] = bytes.fromhex('000000251f8100001fd1000000b08150')
                    assert p.dll.retro_unserialize(C.create_string_buffer(bytes(state)), len(state))
                    p.ram[0x1a:0x1c] = [0xd5, 0x1f]
                    p.ram[0x1fd6:0x1fdd] = [0, 0, 4, 0x25, 0xe8, 0xc1, 4]
                    p.ram[0x1fdd:0x1fe0] = [0x12, 0x81, 5]
                    p.ram[0x72] = trap
                    # 宝箱スクリプトの終端。効果処理後は通常の探索へ戻す。
                    p.ram[0x73:0x76] = [0, 0x1e, 0x7e]
                    p.ram[0x1e00] = 6
                    p.ram[0x28:0x2e] = [0] * 6
                    p.ram[0x30:0x32] = [0, 0x10]
                    p.ram[0x1054] = 0
                    assert p.dll.wiizv_set_audio_speed(numerator, denominator)
                    run(90)
                    tap(32)
                    tap(32)  # わなをはずす
                    tap(256)  # 先頭のキャラクター
                    tap(256)
                    for _ in range(4):
                        tap(32)  # あくまのめだま、実際は毒の罠
                    tap(256)
                    tap(256)
                    for _ in range(5):
                        if any(p.ram[0x1054 + i * 0x80] > 0 for i in range(6)):
                            break
                        tap(256)
                    if not any(p.ram[0x1054 + i * 0x80] > 0 for i in range(6)): p.snapshot('failure')
                    assert any(p.ram[0x1054 + i * 0x80] > 0 for i in range(6)), (numerator, denominator, mask, trap, 'poison not applied')
                    for _ in range(20):
                        tap(256)
                        if p.ram[0x1a] == 0xd8 and p.ram[0x1b] == 0x1f:
                            break
                    result = p.state()
                    offset = result.index(b'REG:000016:') + 11
                    pc = (result[offset] << 16) | int.from_bytes(result[offset + 14:offset + 16], 'big')
                    assert pc >> 16 != 0x0a, (numerator, denominator, mask, trap, hex(pc))
                    assert p.ram[0x1a] == 0xd8 and p.ram[0x1b] == 0x1f, (hex(pc), bytes(p.ram[0x1a:0x1c]).hex())
                    print(f'PASS: speed={numerator}/{denominator}, effects={mask}, trap={trap}: poison + return to exploration', flush=True)
        p.snapshot('complete')
    finally:
        p.close()


if __name__ == '__main__':
    main()



