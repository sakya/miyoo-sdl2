#!/usr/bin/env python3
"""Reference vectors for straight-alpha SDL_BLENDMODE_BLEND on ARGB8888.
No claim of hardware validation: compare device readback against these vectors.
"""
def blend(src, dst):
    sa = (src >> 24) & 255
    da = (dst >> 24) & 255
    def channel(shift):
        s, d = (src >> shift) & 255, (dst >> shift) & 255
        return (s * sa + d * (255 - sa) + 127) // 255
    a = sa + (da * (255 - sa) + 127) // 255
    return (a << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0)

def main():
    cases = [(0x00FF0000, 0xFF0000FF), (0xFFFF0000, 0xFF0000FF),
             (0x80FF0000, 0xFF0000FF), (0x40FFFFFF, 0x80000000),
             (0xC000FF00, 0x00000000)]
    for src, dst in cases:
        print(f'src={src:08X} dst={dst:08X} expected={blend(src,dst):08X}')
    assert blend(0x00FF0000, 0xFF0000FF) == 0xFF0000FF
    assert blend(0xFFFF0000, 0xFF0000FF) == 0xFFFF0000
    assert blend(0x80FF0000, 0xFF0000FF) == 0xFF80007F
if __name__ == '__main__': main()
