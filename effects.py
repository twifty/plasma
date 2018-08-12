from packet import Packet
from packet.blob import RGB, BYTE


class Caps():
    COLOR = 0x01
    RANDOM = 0x02
    BRIGHTNESS = 0x04
    SPEED = 0x08
    DIRECTION = 0x10
    MIRAGE = 0x20
    REPEAT = 0x40

    def __init__(self, flags=0):
        self.flags = flags

    def Color(self) -> bool:
        return self.flags & Caps.COLOR

    def Random(self) -> bool:
        return self.flags & Caps.RANDOM

    def Brightness(self) -> bool:
        return self.flags & Caps.BRIGHTNESS

    def Speed(self) -> bool:
        return self.flags & Caps.SPEED

    def Direction(self) -> bool:
        return self.flags & Caps.DIRECTION

    def Mirage(self) -> bool:
        return self.flags & Caps.MIRAGE

    def Repeat(self) -> bool:
        return self.flags & Caps.REPEAT


class Speed(BYTE):
    # TODO What are the upper two bits for?
    map = {
        1: 0x72,  # Slow
        2: 0x68,
        3: 0x64,
        4: 0x62,
        5: 0x61   # Fast
    }

    def __init__(self, value):
        self.level = 0
        if isinstance(value, BYTE):
            for level in Speed.map:
                if Speed.map[level] & value:
                    self.level = level
                    break
        else:
            self.level = max(1, min(5, value))

    def __add__(self, other):
        self.level = max(1, min(5, self.level + int(other)))

    def __sub__(self, other):
        self.level = max(1, min(5, self.level - int(other)))

    def value(self) -> BYTE:
        return BYTE(Speed.map[self.level])


class Effect():
    def __init__(self, settings: Packet.Profile.EffectSettings):
        # type_check(settings, Packet.Profile.EffectSettings)
        self.settings = settings

    def getCaps(self) -> Caps:
        return Caps()

    def setBrightness(self, level: int) -> None:
        self.settings.setParam(5, BYTE(level))

    def getBrightness(self) -> int:
        return self.settings.getParam(5)


class Static(Effect):
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.BRIGHTNESS)

    def setColor(self, color: RGB) -> None:
        self.settings.setRGB(1, color)

    def getColor(self) -> RGB:
        return self.settings.getRGB(1)


class Rainbow(Effect):
    def getCaps(self) -> Caps:
        return Caps(Caps.SPEED | Caps.BRIGHTNESS)

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value())

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1))


class Cycle(Rainbow):
    pass


class Bounce(Rainbow):
    pass


class Breathing(Rainbow):
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.SPEED | Caps.BRIGHTNESS)

    def setColor(self, color: RGB) -> None:
        self.settings.setRGB(1, color)

    def getColor(self) -> RGB:
        return self.settings.getRGB(1)

    def setRandom(self, on: bool = True) -> None:
        if on:
            self.settings.orParam(2, 0x80)
        else:
            self.settings.notParam(2, 0x80)

    def isRandom(self) -> bool:
        return self.settings.getParam(2) & 0x80


class Swirl(Breathing):
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.SPEED | Caps.DIRECTION | Caps.BRIGHTNESS)

    def setClockwise(self, on: bool = True) -> None:
        if on:
            self.settings.orParam(2, 0x01)
        else:
            self.settings.notParam(2, 0x01)


class Chase(Swirl):
    pass


class Morse(Static):
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.REPEAT | Caps.BRIGHTNESS)

    def setRandom(self, on: bool = True) -> None:
        if on:
            self.settings.orParam(2, 0x80)
        else:
            self.settings.notParam(2, 0x80)

    def isRandom(self) -> bool:
        return self.settings.getParam(2) & 0x80

    def setRepeat(self, on: bool = True) -> None:
        if on:
            self.settings.setParam(4, 0xFF)
        else:
            self.settings.setParam(4, 0x01)

    def isReapeat(self) -> bool:
        return self.settings.getParam(4) == 0xFF
