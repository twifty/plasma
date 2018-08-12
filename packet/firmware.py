from argparse import Namespace
from .raw import Raw
from debug import validate

MODE_READ = 0x12


class Base(Raw):
    def displayName(self):
        return 'Packet.Firmware.%s' % self.__class__.__name__


# 0x12 0x00 - Firmware details
class Query(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x00)

    def decode(self, blob):
        self.flags = validate(blob.readWord(), 0x00000004)
        self.unknown_01 = validate(blob.readDword(), 0x00000000)
        self.unknown_02 = validate(blob.readDword(), 0x00000000)
        self.vendorId = validate(blob.readWord(), 0x2516)
        self.productId = validate(blob.readWord(), 0x0052)
        self.unknown_03 = validate(blob.readDword(), 0x04087000)
        self.unknown_04 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_05 = validate(blob.readDword(), 0x00000001)
        self.unknown_06 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_07 = validate(blob.readDword(), 0x000000E0)
        self.unknown_08 = validate(blob.readDword(), 0x00E70200)
        self.unknown_09 = validate(blob.readWord(), 0x0200)
        self.firmwareName = validate(blob.readBytes(10).decode('ascii'), 'LM0303')
        self.unknown_12 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_13 = validate(blob.readDword(), 0x00000000)
        self.unknown_14 = validate(blob.readDword(), 0x00000000)


# 0x12 0x20 - The firmware version string
class Version(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x20)

    def decode(self, blob):
        self.flags = validate(blob.readWord(), 0x0000)
        self.size = validate(blob.readDword(), 0x0000001A)

        if self.size > 0:
            self.versionStr = validate(blob.readBytes(self.size).decode('utf16'), u'V1.01.00')
        else:
            self.versionStr = ''

        blob.readBytes(64 - (8 + self.size))


# 0x12 0x01 - Contains some flags
class Anon_01(Base):
    # 0x12, 0x01, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00,
    def __init__(self):
        super().__init__(MODE_READ, 0x01)

    def decode(self, blob):
        self.unknown_01 = validate(blob.readWord(), 0x0000)
        self.unknown_02 = validate(blob.readWord(), 0x0004)
        self.unknown_03 = validate(blob.readWord(), 0x0002)

        blob.readBytes(56)


# 0x12 0x22 - More firmware parameters
class Anon_22(Base):
    def __init__(self):
        super().__init__(MODE_READ, 0x22)

    def decode(self, blob):
        self.unknown_01 = validate(blob.readWord(), 0x0000)
        self.unknown_02 = validate(blob.readWord(), 0x0004)
        self.unknown_03 = validate(blob.readWord(), 0x0080)
        self.unknown_04 = validate(blob.readWord(), 0x0100)
        self.unknown_05 = validate(blob.readWord(), 0x0001)
        self.unknown_06 = validate(blob.readWord(), 0x00E0)
        self.unknown_07 = validate(blob.readWord(), 0x0000)
        self.unknown_08 = validate(blob.readDword(), 0xEFFFFFFF)
        self.unknown_09 = validate(blob.readDword(), 0x00000001)
        self.unknown_10 = validate(blob.readDword(), 0x00000000)
        self.vendorId = validate(blob.readWord(), 0x2516)
        self.productId = validate(blob.readWord(), 0x0051)
        self.unknown_11 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_12 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_13 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_14 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_15 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_16 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_17 = validate(blob.readDword(), 0xFFFFFFFF)
        self.unknown_18 = validate(blob.readDword(), 0x001C5AA5)


Firmware = Namespace(
    Query=Query,
    Version=Version,
    Anon_01=Anon_01,
    Anon_22=Anon_22
)
