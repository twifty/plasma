from argparse import Namespace
from .raw import Raw
from .blob import Blob
from .types import BYTE, WORD, RGB, Resolution
from debug import validate, validate_range, validate_tuple

MODE_FIRMWARE = 0x50
MODE_WRITE = 0x51
MODE_READ = 0x52

"""
    0x28 - Active effect/profile? Always 0xE0 and always sent before modifying values.
    0x29
    0x2C - Read/Write effect parameters
    0x70 - Mirage RGB values
    0x71 - Breathing settings?
    0x73 - Morse code pages
    0x94 - Contains three RGB triplets for Mirage (3 stored settings) These are in 45 - 2000 range
    0x96 - Always follows 0x4103 or 0x4180, always zeros
    0xA0 - Active effects (0x05 and 0x06 always seem to be active)
"""


class Base(Raw):
    def displayName(self) -> str:
        return 'Packet.Profile.%s' % self.__class__.__name__


# 0x52 0x96
class Anon_96(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x96)

    def decode(self, blob: Blob) -> None:
        n = 0
        while n < 31:
            validate(blob.readWord(), 0x0000)
            n += 1


# 0x52 0x29 - Read once during application start
class Anon_29(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x29)

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(0x0000))
        self.unknown_02 = validate(blob.readWord(0x00E0))


# 0x52/0x51 0x28 - The description could be wrong
class Active(Base):
    """Stores the active profile ID.

       This should be read before being used when changing settings.

    Parameters
    ----------
    mode : int
        MODE_WRITE or MODE_READ.

    """

    def __init__(self, mode=MODE_WRITE):
        super().__init__(mode, 0x28)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.unknown_01)
        blob.writeBytes(self.id)

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(), 0x0000)
        self.id = blob.readWord()  # This could easily be a byte

        blob.readBytes(58)


# 0x51/0x52 0x2C
class EffectSettings(Base):
    """Used by all effects.

    Parameters
    ----------
    mode : int
        MODE_WRITE or MODE_READ.

    """

    def __init__(self, mode=MODE_WRITE):
        super().__init__(mode, 0x2C)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.unknown_01)
        blob.writeBytes(self.id)
        blob.writeBytes(self.p1)
        blob.writeBytes(self.p2)
        blob.writeBytes(self.p3)
        blob.writeBytes(self.p4)
        blob.writeBytes(self.p5)
        blob.writeBytes(self.rgb_1)
        blob.writeBytes(self.rgb_2)
        blob.writeBytes(self.padding)

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(), 0x0001)
        self.id = blob.readByte()
        self.p1 = blob.readByte()
        self.p2 = blob.readByte()
        self.p3 = blob.readByte()
        self.p4 = blob.readByte()
        self.p5 = blob.readByte()
        self.rgb_1 = RGB(blob.readBytes(3))
        self.rgb_2 = RGB(blob.readBytes(3))
        self.padding = blob.readBytes(46)

    def setId(self, id: BYTE) -> None:
        self.id = id

    def getId(self) -> BYTE:
        return self.id

    def setParam(self, index: int, value: BYTE) -> None:
        assert index >= 1 and index <= 5, "Index out of range"
        setattr(self, "p%d" % index, value)

    def getParam(self, index: int) -> BYTE:
        assert index >= 1 and index <= 5, "Index out of range"
        return getattr(self, "p%d" % index)

    def orParam(self, index: int, value: BYTE) -> None:
        assert index >= 1 and index <= 5, "Index out of range"
        existing = getattr(self, "p%d" % index)
        existing |= value
        setattr(self, "p%d" % index, existing)

    def notParam(self, index: int, value: BYTE) -> None:
        assert index >= 1 and index <= 5, "Index out of range"
        existing = getattr(self, "p%d" % index)
        existing &= ~value
        setattr(self, "p%d" % index, existing)

    def setRGB(self, index: int, value: RGB) -> None:
        assert index >= 1 and index <= 2, "Index out of range"
        setattr(self, "rgb_%d" % index, value)

    def getRGB(self, index: int) -> RGB:
        assert index >= 1 and index <= 2, "Index out of range"
        return getattr(self, "rgb_%d" % index)


# 0x51/0x52 0xA0
class ApplyActive(Base):
    def __init__(self, mode=MODE_WRITE):
        super().__init__(mode, 0xA0)

    def encode(self, blob: Blob) -> None:
        return

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(), 0x0001)
        self.unknown_02 = validate(blob.readByte(), 0x00)
        self.count = validate(blob.readByte(), 0x03)  # Third entry will be 0xFE if disabled
        self.unknown_03 = validate(blob.readWord(), 0x0000)
        self.entry_1 = validate(blob.readByte(), 0x05)
        self.entry_2 = validate(blob.readByte(), 0x06)
        self.entry_3 = blob.readByte()

        # NOTE entry_3 is repeated 15 times than followed by zeros
        self.padding = blob.readBytes(53)


# 0x51/0x52 0x70
class BreathPage(Base):
    """ Storage unit for breath cycle pages.

        There are 2 cycles each spanning 5 pages. There may be one for the ring
        and another for the fan/logo. Both zones appear to be slightly different.

        Each page holds 20 RGB triplets except the last which holds 5. That means a
        total of 85 triplets (255 bytes) per zone.

        NOTE The last page of each zone contains a extra RED byte, this is probably
        due to memcpy copying 16 bytes in the original algorithm.

        These pages are written during application startup.
    """

    def __init__(self, zone, index, mode=MODE_WRITE):
        self.zone = BYTE(zone)
        self.index = BYTE(index)
        self.data = None
        super().__init__(mode, 0x70)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.index)
        blob.writeBytes(self.zone)

        if self.data is not None:
            blob.writeBytes(self.data)

    def decode(self, blob: Blob) -> None:
        validate(blob.readByte(), self.index)
        validate(blob.readByte(), self.zone)
        self.data = blob.readBytes(60)


# 0x51/0x52 0x71
class MirageResolution(Base):
    def __init__(self, mode=MODE_WRITE):
        super().__init__(mode, 0x71)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.unknown_01)
        blob.writeBytes(self.index_1)
        blob.writeBytes(self.off)
        blob.writeBytes(self.index_2)
        blob.writeBytes(self.res_r)
        blob.writeBytes(self.index_3)
        blob.writeBytes(self.res_g)
        blob.writeBytes(self.index_4)
        blob.writeBytes(self.res_b)
        blob.writeBytes(self.padding)

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(), 0x0000)

        self.index_1 = validate(blob.readByte(), 0x01)
        self.off = Resolution(validate(blob.readBytes(3), b'\x00\xFF\x4A'))
        self.index_2 = validate(blob.readByte(), 0x02)
        self.res_r = Resolution(blob.readBytes(3))
        self.index_3 = validate(blob.readByte(), 0x03)
        self.res_g = Resolution(blob.readBytes(3))
        self.index_4 = validate(blob.readByte(), 0x04)
        self.res_b = Resolution(blob.readBytes(3))

        self.padding = blob.readBytes(44)

    def getResolutions(self) -> tuple:
        return (
            self.res_r,
            self.res_g,
            self.res_b
        )

    def setResolutions(self, values: None) -> None:
        if values is None:
            self.res_r = self.res_g = self.res_b = self.off
        else:
            validate_tuple(values, (Resolution, Resolution, Resolution))
            self.res_r = values[0]
            self.res_g = values[1]
            self.res_b = values[2]

    # def setResolution(self, which: str, value: Resolution) -> None:
    #     assert which in ['r', 'g', 'b'], "Unknown offset '%s', expected 'r', 'g' or 'b'" % which
    #     setattr(self, 'res_%s' % which, value)
    #
    # def getResolution(self, which: str) -> Resolution:
    #     assert which in ['r', 'g', 'b'], "Unknown offset '%s', expected 'r', 'g' or 'b'" % which
    #     return getattr(self, 'res_%s' % which)


# 0x51/0x52 0x73
class MorsePage(Base):
    """Holds a single page of morse code data.

       There are a total of 8 pages available. Page 0 and 1 are the active pages.
       Pages 2 - 7, are split between the 3 memory slots. The active pages are
       repeated in these slots.

       p4 in the morse profile is set to 0xFF for repeat, and 0x01 for once (possibly a count).

       There appears to be 60 bytes available on each page, making 120 bytes available
       to the sequence.

       The morse alphabet consists of 4 letters (2 bits each):
           0x00 - space
           0x01 - dot
           0x02 - dash
           0x03 - end
       Each series of dots and dashes terminates with a space. The whole sequence terminates with an end.
       1 byte holds 4 letters (in reverse) -- They are shifted onto the byte.

    Parameters
    ----------
    mode : int
        MODE_WRITE or MODE_READ.

    """

    def __init__(self, mode=MODE_WRITE):
        super().__init__(mode, 0x73)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.index)
        blob.writeBytes(self.data)

    def decode(self, blob: Blob) -> None:
        self.index = blob.readWord()
        self.data = blob.readBytes(60)


# 0x52 0x94
class MirageFrequencies(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x94)

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.unknown_01)
        blob.writeBytes(self.count)
        blob.writeBytes(self.r_1)
        blob.writeBytes(self.g_1)
        blob.writeBytes(self.b_1)
        blob.writeBytes(self.r_2)
        blob.writeBytes(self.g_2)
        blob.writeBytes(self.b_2)
        blob.writeBytes(self.r_3)
        blob.writeBytes(self.g_3)
        blob.writeBytes(self.b_3)

    def decode(self, blob: Blob) -> None:
        self.unknown_01 = validate(blob.readWord(), 0x0000)
        self.count = validate(blob.readByte(), 0x03)
        self.r_1 = blob.readWord()
        self.g_1 = blob.readWord()
        self.b_1 = blob.readWord()
        self.r_2 = blob.readWord()
        self.g_2 = blob.readWord()
        self.b_2 = blob.readWord()
        self.r_3 = blob.readWord()
        self.g_3 = blob.readWord()
        self.b_3 = blob.readWord()

    def getFrequency(self, slot: int) -> tuple:
        validate_range(slot, 0, 2)
        return (
            getattr(self, 'r_%d' % slot),
            getattr(self, 'g_%d' % slot),
            getattr(self, 'b_%d' % slot),
        )

    def setFrequency(self, slot: int, value: tuple) -> None:
        validate_range(slot, 0, 2)
        validate_tuple(value, (int, int, int))
        setattr(self, 'r_%d' % slot, WORD(value[0]))
        setattr(self, 'g_%d' % slot, WORD(value[1]))
        setattr(self, 'b_%d' % slot, WORD(value[2]))


Profile = Namespace(
    MODE_FIRMWARE=MODE_FIRMWARE,
    MODE_READ=MODE_READ,
    MODE_WRITE=MODE_WRITE,

    Anon_96=Anon_96,
    Anon_29=Anon_29,

    EffectSettings=EffectSettings,
    ApplyActive=ApplyActive,
    BreathPage=BreathPage,
    MorsePage=MorsePage,
    MirageFrequencies=MirageFrequencies,
    MirageResolution=MirageResolution,
    Active=Active
)
