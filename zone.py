from copy import deepcopy
from effects import Effect, Mirage, Static, Rainbow, Swirl, \
    Chase, Bounce, Morse, Cycle, Breathing, Off
from packet.types import BYTE


class Caps():
    STATIC = 0x01
    RAINBOW = 0x02
    SWIRL = 0x04
    CHASE = 0x08
    BOUNCE = 0x10
    MORSE = 0x20
    CYCLE = 0x40
    BREATHING = 0x80
    OFF = 0x100

    def __init__(self, flags=0):
        self.flags = flags

    def Static(self) -> bool:
        return self.flags & Caps.STATIC

    def Rainbow(self) -> bool:
        return self.flags & Caps.RAINBOW

    def Swirl(self) -> bool:
        return self.flags & Caps.SWIRL

    def Chase(self) -> bool:
        return self.flags & Caps.CHASE

    def Bounce(self) -> bool:
        return self.flags & Caps.BOUNCE

    def Morse(self) -> bool:
        return self.flags & Caps.MORSE

    def Cycle(self) -> bool:
        return self.flags & Caps.CYCLE

    def Breathing(self) -> bool:
        return self.flags & Caps.BREATHING

    def Off(self) -> bool:
        return self.flags & Caps.OFF

    def validate(self, effect: Effect) -> bool:
        for key in Caps.typeMap:
            # NOTE ignore parent types here!
            if key & self.flags and type(effect) == Caps.typeMap[key]:
                return True
        return False


Caps.typeMap = {
    Caps.STATIC: Static,
    Caps.RAINBOW: Rainbow,
    Caps.SWIRL: Swirl,
    Caps.CHASE: Chase,
    Caps.BOUNCE: Bounce,
    Caps.MORSE: Morse,
    Caps.CYCLE: Cycle,
    Caps.BREATHING: Breathing,
    Caps.OFF: Off
}


class Zone():
    def __init__(self, effect: Effect):
        self._copyEffect(effect)

    def _copyEffect(self, effect: Effect) -> None:
        if not self.getCaps().validate(effect):
            raise TypeError("Effect %s cannot be applied to zone %s." %
                            (type(effect).__name__, type(self).__name__))
        self.effect = deepcopy(effect)
        self.effect.resetIds()  # Id and p3 sometimes differ between RGB and ARGB usage

    def getCaps(self) -> Caps:
        raise NotImplementedError("The developer got lazy and forgot to write a method body!")

    def applyEffect(self, effect: Effect) -> None:
        raise NotImplementedError("The developer got lazy and forgot to write a method body!")


# Effect 0x00 0x07 0x0A 0x09 0x08 0x0B 0x02 0x01 (0xFE off)
class Ring(Zone):
    def getCaps(self) -> Caps:
        return Caps(Caps.STATIC | Caps.RAINBOW | Caps.SWIRL | Caps.CHASE |
                    Caps.BOUNCE | Caps.MORSE | Caps.CYCLE | Caps.BREATHING | Caps.OFF)

    def applyEffect(self, effect: Effect) -> None:
        self._copyEffect(effect)


# Effect 0x06
class Logo(Zone):
    def getCaps(self) -> Caps:
        return Caps(Caps.STATIC | Caps.CYCLE | Caps.BREATHING | Caps.OFF)

    def _getId(self) -> BYTE:
        return BYTE(0x06)

    def applyEffect(self, effect: Effect) -> None:
        self._copyEffect(effect)
        settings = self.effect.getSettings()
        if type(effect) == Off:
            settings.setParam(3, BYTE(0))
        elif type(effect) == Static:
            settings.setParam(3, BYTE(1))
        elif type(effect) == Cycle:
            settings.setParam(3, BYTE(2))
        elif type(effect) == Breathing:
            settings.setParam(3, BYTE(3))
        settings.setId(self._getId())


# Effect 0x05
# NOTE packet 0x94 is wrong (see c code)
class Fan(Logo):
    def __init__(self, effect: Effect, mirage: Mirage = None):
        super().__init__(effect)
        self.setMirage(mirage)

    def _getId(self) -> BYTE:
        return BYTE(0x05)

    def getMirage(self) -> Mirage:
        return self.mirage

    def setMirage(self, mirage: Mirage):
        self.mirage = deepcopy(mirage)
