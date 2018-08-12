from driver import Driver
from packet import Packet


def toHex(value): return "".join("0x{:02X} ".format(c) for c in value)


VENDOR_ID = 0x2516
PRODUCT_ID = 0x0051

usb = Driver(VENDOR_ID, PRODUCT_ID)
del Driver


query = Packet.Profile.BreathPage(0, 4, Packet.Profile.MODE_READ)
# query.dump()
print(query)
usb.post(query)

print(query)

# request = Packet.Firmware.Version()
# response = usb.post(request)
# print(response)
