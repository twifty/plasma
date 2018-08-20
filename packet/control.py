from argparse import Namespace
from .raw import Raw
from .blob import Blob
from .types import WORD
from debug import validate


MODE_FIRMWARE = 0x40
MODE_WRITE = 0x41
MODE_READ = 0x42


class Base(Raw):
    def displayName(self) -> str:
        return 'Packet.Control.%s' % self.__class__.__name__


# 0x42 0x00 0x00 0x00 - Write - zeros memory?
class Anon_00(Base):
    # 42 00 00 00 01 00 00 01 01 00 00 00 00 00 00 00
    def __init__(self):
        super().__init__(MODE_READ, 0x00, 0x00, 0x00)

        self.unknown_02 = WORD(b'\x01\x00')
        self.unknown_03 = WORD(b'\x00\x01')
        self.unknown_04 = WORD(b'\x01\x00')
        self.unknown_05 = WORD(b'\x00\x00')
        self.unknown_06 = WORD(b'\x00\x00')
        self.unknown_07 = WORD(b'\x00\x00')

    def encode(self, blob: Blob) -> None:
        blob.writeBytes(self.unknown_02)
        blob.writeBytes(self.unknown_03)
        blob.writeBytes(self.unknown_04)
        blob.writeBytes(self.unknown_05)
        blob.writeBytes(self.unknown_06)
        blob.writeBytes(self.unknown_07)

    def decode(self, blob: Blob) -> None:
        validate(blob.readWord(), self.unknown_02)
        validate(blob.readWord(), self.unknown_03)
        validate(blob.readWord(), self.unknown_04)
        validate(blob.readWord(), self.unknown_05)
        validate(blob.readWord(), self.unknown_06)
        validate(blob.readWord(), self.unknown_07)


# 0x41 0x00 0x00 0x00 - Read - once during app start (with data)
class Reset(Base):
    def __init__(self):
        super().__init__(MODE_WRITE, 0x00, 0x00, 0x00)


# 0x41 0x03 0x00 0x00 - Used during app start to read all settings, and "apply" to write changes
class Stored(Base):
    def __init__(self):
        super().__init__(MODE_WRITE, 0x03, 0x00, 0x00)


# 0x40 0x20 0x00 0x00 - Used to fetch details about the effects
class EffectDetails(Base):
    def __init__(self):
        super().__init__(MODE_FIRMWARE, 0x20, 0x00, 0x00)

    def decode(self, blob: Blob) -> None:
        self.unknown_02 = blob.readWord()   # 0x0001
        self.unknown_03 = blob.readByte()   # 0x02
        self.count = blob.readByte()        # 0x000D (Num of effects)
        self.unknown_02 = blob.readDword()  # 0x00000100
        self.unknown_02 = blob.readDword()  # 0x00000001


# 0x40 0x21 <index> 0x00 - Used to return effect names (also a few flags)
class EffectName(Base):
    def __init__(self, index: int):
        super().__init__(MODE_FIRMWARE, 0x21, index, 0x00)

    def decode(self, blob: Blob) -> None:
        self.unknown_02 = blob.readDword()  # 0x00000329 - bit set
        self.name = blob.readBytes(56).decode('ascii')  # null terminated string


# 0x41 0x80 0x00 0x00 - Used any time the active effect is changed
class Active(Base):
    def __init__(self):
        super().__init__(MODE_WRITE, 0x80, 0x00, 0x00)


Control = Namespace(
    Anon_00=Anon_00,

    EffectDetails=EffectDetails,
    EffectName=EffectName,
    Reset=Reset,
    Stored=Stored,
    Active=Active
)
