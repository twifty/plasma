
class Caps():
    STATIC = 0x01
    RAINBOW = 0x02
    SWIRL = 0x04
    CHASE = 0x08
    BOUNCE = 0x10
    MORSE = 0x20
    CYCLE = 0x40
    BREATHING = 0x80
    MIRAGE = 0x100

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

    def Mirage(self) -> bool:
        return self.flags & Caps.MIRAGE


class Zone():
    pass


class Ring(Zone):
    def getCaps() -> Caps:
        return Caps(Caps.STATIC | Caps.RAINBOW | Caps.SWIRL | Caps.CHASE |
                    Caps.BOUNCE | Caps.MORSE | Caps.CYCLE | Caps.BREATHING)


class Logo(Zone):
    def getCaps() -> Caps:
        return Caps(Caps.STATIC | Caps.CYCLE | Caps.BREATHING)


class Fan(Logo):
    def getCaps() -> Caps:
        return Caps(Caps.STATIC | Caps.CYCLE | Caps.BREATHING | Caps.MIRAGE)
