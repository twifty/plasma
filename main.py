from driver import Driver
from packet import Packet
from debug import validate_tuple
from effects import Morse


def toHex(value): return "".join("0x{:02X} ".format(c) for c in value)


VENDOR_ID = 0x2516
PRODUCT_ID = 0x0051


str = Morse.Encoder.encode('owen parry')
print(str)
print(Morse.Encoder.decode(str))
raw = Morse.Encoder.to_bytes(str)
print(toHex(raw))
print(Morse.Encoder.from_bytes(raw))


# usb = Driver(VENDOR_ID, PRODUCT_ID)
# del Driver
#
#
# query = Packet.Profile.BreathPage(0, 4, Packet.Profile.MODE_READ)
# # query.dump()
# print(query)
# usb.post(query)
#
# print(query)

# request = Packet.Firmware.Version()
# response = usb.post(request)
# print(response)
