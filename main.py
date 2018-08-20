from driver import Driver
from packet import Packet
from packet.types import RGB
from zone import Fan
from debug import validate_tuple
from effects import Effect


def toHex(value): return "".join("0x{:02X} ".format(c) for c in value)


VENDOR_ID = 0x2516
PRODUCT_ID = 0x0051


usb = Driver(VENDOR_ID, PRODUCT_ID)

# To change a profile, the full sequqnce of packets must be sent

# Get the active fan profile
profile = Packet.Profile.EffectSettings(0x05, Packet.Profile.MODE_READ)
response = usb.post(profile)
# profile.dump()
print(response)

effect = Effect.Factory(profile)
fan = Fan(effect)

# Change the color
red = RGB(0xFF, 0x00, 0x00)
profile.setRGB(1, red)
profile.setMode(Packet.Profile.MODE_WRITE)
print(profile)

mode = Packet.Profile.Anon_28()
response = usb.post(mode)
print(response)

response = usb.post(profile)
print(response)


# apply the new profile


# query = Packet.Profile.BreathPage(0, 4, Packet.Profile.MODE_READ)
# # query.dump()
# print(query)
# usb.post(query)
#
# print(query)

# request = Packet.Firmware.Version()
# response = usb.post(request)
# print(response)

usb = None
