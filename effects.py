from packet import Packet
from packet.types import Resolution, RGB, BYTE
from debug import validate_range, validate_type, validate_tuple


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
    """Helper class for configuring speed across effects.

       The actual speed values (written to p1) differ across
       multiple effects, and even differ between steps.

       As far as I can tell, p1 is only used for speed values. It may
       be possible to use other values for a more fine grained control.

    Parameters
    ----------
    value : int
        A value between 1 (slow) and 5 (fast).

    Attributes
    ----------
    level : int
        The speed level, between 1 for slow and 5 for fast.
    profiles : tuple
        Contains the actual values for each speed level.

    """
    profiles = ({
        # Used by Swirl, Chase, Bounce
        1: 0x77, 2: 0x74, 3: 0x6E, 4: 0x6B, 5: 0x67
    }, {
        # Used by Rainbow
        1: 0x72, 2: 0x68, 3: 0x64, 4: 0x62, 5: 0x61
    }, {
        # Used by Breathing
        1: 0x3C, 2: 0x37, 3: 0x31, 4: 0x2C, 5: 0x26
    }, {
        # Used by Cycle
        1: 0x96, 2: 0x8C, 3: 0x80, 4: 0x6E, 5: 0x68
    })

    def __init__(self, value, profile: int = None):
        self.level = 5
        if isinstance(profile, int):
            validate_type(value, BYTE)
            for level in Speed.profiles[profile]:
                if Speed.profiles[profile][level] & value:
                    self.level = level
                    break
        else:
            self.level = max(1, min(5, value))

    def __add__(self, other):
        self.level = max(1, min(5, self.level + int(other)))

    def __sub__(self, other):
        self.level = max(1, min(5, self.level - int(other)))

    def value(self, profile: int) -> BYTE:
        validate_range(profile, 0, 4)
        return BYTE(Speed.profiles[profile][self.level])


class Brightness(BYTE):
    profiles = ({
        # Used by all except Cycle
        1: 0x4C, 2: 0x99, 3: 0xFF
    }, {
        # Used by Cycle
        1: 0x10, 2: 0x40, 3: 0x7F
    })

    def __init__(self, value, profile: int = None):
        self.level = 5
        if isinstance(profile, int):
            validate_type(value, BYTE)
            for level in Brightness.profiles[profile]:
                if Brightness.profiles[profile][level] & value:
                    self.level = level
                    break
        else:
            self.level = max(1, min(3, value))

    def __add__(self, other):
        self.level = max(1, min(3, self.level + int(other)))

    def __sub__(self, other):
        self.level = max(1, min(3, self.level - int(other)))

    def value(self, profile: int) -> BYTE:
        validate_range(profile, 0, 1)
        return BYTE(Brightness.profiles[profile][self.level])


class Mirage():
    def __init__(self, resolution: Packet.Profile.MirageResolution, frequencies: Packet.Profile.MirageFrequencies):
        self.resolution = resolution
        self.frequencies = frequencies

        # TODO Is the active frequency passed anywhere?
        # We can only compare values, but profiles may have the same frequencies
        (r, g, b) = resolution.getResolutions()

        i = 0
        self.active = None

        while i < 3:
            (_r, _g, _b) = frequencies.getFrequency(i)
            if _r == r.to_hertz() and _g == g.to_hertz() and _b == b.to_hertz():
                self.active = i
                break
            i += 1

        if self.active is None:
            self.setActiveProfile(0)

    def setActiveProfile(self, index: None) -> None:
        res = None
        if index is not None:
            validate_range(index, 0, 2)
            (r, g, b) = self.frequencies.getFrequency(index)
            res = (
                Resolution.from_hertz(r),
                Resolution.from_hertz(g),
                Resolution.from_hertz(b)
            )
        self.resolution.setResolutions(res)
        self.active = index

    def getActiveProfile(self):
        return self.active

    def setProfileFrequencies(self, index: int, value: tuple) -> None:
        validate_range(index, 0, 2)
        validate_tuple(value, (int, int, int))
        r = Resolution.from_hertz(value[0])
        g = Resolution.from_hertz(value[1])
        b = Resolution.from_hertz(value[2])
        self.frequencies.setFrequency(index, (r.to_hertz(), g.to_hertz(), b.to_hertz()))
        if index == self.active:
            self.resolution.setResolutions((r, g, b))

    def getProfileFrequencies(self, index: int) -> tuple:
        validate_range(index, 0, 2)
        return self.frequencies.getFrequency(index)


class Effect():
    BRIGHTNESS_PROFILE = 0

    def Factory(settings: Packet.Profile.EffectSettings) -> 'Effect':
        if settings.id == 0x05 or settings.id == 0x06:
            if settings.p3 == 0x01:
                return Static(settings)
            elif settings.p3 == 0x02:
                return Cycle(settings)
            elif settings.p3 == 0x03:
                return Breathing(settings)
        elif settings.id == 0x00:
            return Static(settings)
        elif settings.id == 0x01:
            return Breathing(settings)
        elif settings.id == 0x02:
            return Cycle(settings)
        elif settings.id == 0x07:
            return Rainbow(settings)
        elif settings.id == 0x08:
            return Bounce(settings)
        elif settings.id == 0x09:
            return Chase(settings)
        elif settings.id == 0x0A:
            return Swirl(settings)
        elif settings.id == 0x0B:
            return Morse(settings)
        return Off(settings)

    def __init__(self, settings: Packet.Profile.EffectSettings):
        self.settings = settings

    def getCaps(self) -> Caps:
        return Caps()

    def getSettings(self) -> Packet.Profile.EffectSettings:
        return self.settings

    def setBrightness(self, value: Brightness) -> None:
        self.settings.setParam(5, value.value(Effect.BRIGHTNESS_PROFILE))

    def getBrightness(self) -> Brightness:
        return Brightness(self.settings.getParam(5), Effect.BRIGHTNESS_PROFILE)

    def resetIds(self) -> None:
        raise NotImplementedError("The developer got lazy and forgot to write a method body!")


class Off(Effect):
    def resetIds(self) -> None:
        self.settings.setId(BYTE(0xFE))
        self.settings.setParam(3, BYTE(0x00))


class Static(Effect):
    # When used in the Ring zone, p3 is always 0xFF; 0x01 in the RGB zones
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.BRIGHTNESS)

    def setColor(self, color: RGB) -> None:
        self.settings.setRGB(1, color)

    def getColor(self) -> RGB:
        return self.settings.getRGB(1)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x00))
        self.settings.setParam(3, BYTE(0xFF))


class Rainbow(Effect):
    # p3 is always 0x05
    SPEED_PROFILE = 1

    def getCaps(self) -> Caps:
        return Caps(Caps.SPEED | Caps.BRIGHTNESS)

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value(Rainbow.SPEED_PROFILE))

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1), Rainbow.SPEED_PROFILE)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x07))
        self.settings.setParam(3, BYTE(0x05))


class Cycle(Rainbow):
    # When used in the Ring zone, p3 is always 0xFF; 0x02 in the RGB zones
    SPEED_PROFILE = 3
    BRIGHTNESS_PROFILE = 1

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value(Cycle.SPEED_PROFILE))

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1), Cycle.SPEED_PROFILE)

    def setBrightness(self, value: Brightness) -> None:
        self.settings.setParam(5, value.value(Cycle.BRIGHTNESS_PROFILE))

    def getBrightness(self) -> Brightness:
        return Brightness(self.settings.getParam(5), Cycle.BRIGHTNESS_PROFILE)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x02))
        self.settings.setParam(3, BYTE(0xFF))


class Bounce(Rainbow):
    # p3 is always 0xFF
    SPEED_PROFILE = 0

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value(Bounce.SPEED_PROFILE))

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1), Bounce.SPEED_PROFILE)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x08))
        self.settings.setParam(3, BYTE(0xFF))


class Breathing(Rainbow):
    # p3 is always 0x03 (no matter where used)
    SPEED_PROFILE = 2

    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.SPEED | Caps.BRIGHTNESS)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x01))
        self.settings.setParam(3, BYTE(0x03))

    def setColor(self, color: RGB) -> None:
        self.settings.setRGB(1, color)

    def getColor(self) -> RGB:
        return self.settings.getRGB(1)

    def setRandom(self, on: bool = True) -> None:
        # p2 Always has the 0x20 bit set for breathing
        if on:
            self.settings.orParam(2, 0x80)
        else:
            self.settings.notParam(2, 0x80)

    def isRandom(self) -> bool:
        return self.settings.getParam(2) & 0x80

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value(Breathing.SPEED_PROFILE))

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1), Breathing.SPEED_PROFILE)


class Swirl(Breathing):
    # p3 is always 4A
    SPEED_PROFILE = 0

    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.SPEED | Caps.DIRECTION | Caps.BRIGHTNESS)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x0A))
        self.settings.setParam(3, BYTE(0x4A))

    def setClockwise(self, on: bool = True) -> None:
        if on:
            self.settings.notParam(2, 0x01)
        else:
            self.settings.orParam(2, 0x01)

    def setSpeed(self, value: Speed) -> None:
        self.settings.setParam(1, value.value(Swirl.SPEED_PROFILE))

    def getSpeed(self) -> Speed:
        return Speed(self.settings.getParam(1), Swirl.SPEED_PROFILE)


class Chase(Swirl):
    # p3 is always 0xc3
    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x09))
        self.settings.setParam(3, BYTE(0xC3))


class Morse(Static):
    class Encoder():
        ALPHABET = {
            'A': '.-', 'B': '-...', 'C': '-.-.', 'D': '-..', 'E': '.', 'F': '..-.',
            'G': '--.', 'H': '....', 'I': '..', 'J': '.---', 'K': '-.-', 'L': '.-..',
            'M': '--', 'N': '-.', 'O': '---', 'P': '.--.', 'Q': '--.-', 'R': '.-.',
            'S': '...', 'T': '-', 'U': '..-', 'V': '...-', 'W': '.--', 'X': '-..-',
            'Y': '-.--', 'Z': '--..', '1': '.----', '2': '..---', '3': '...--',
            '4': '....-', '5': '.....', '6': '-....', '7': '--...', '8': '---..',
            '9': '----.', '0': '-----', ',': '--..--', '.': '.-.-.-', '?': '..--..',
            '/': '-..-.', '-': '-....-', '(': '-.--.', ')': '-.--.-', ' ': ' '
        }

        def encode(message: str) -> str:
            # 60 bytes are available with each bytes able to store 4 code points.
            # Max length of output is 60 * 4 - 1 (for the terminating 0x03)
            # Discard any ascii letters which run over this size

            maxlen = (60 * 4) - 1
            currlen = 0
            code = ''
            for char in message.upper():
                if char in Morse.Encoder.ALPHABET:
                    sequence = Morse.Encoder.ALPHABET[char] + ' '
                    seqlen = len(sequence)
                    if currlen + seqlen > maxlen:
                        break
                    code += sequence
                    currlen += seqlen
                # silently skip unknown characters
            return code

        def decode(code: str) -> str:
            table = dict((v, k) for k, v in Morse.Encoder.ALPHABET.items())
            result = ''
            for word in code.split('  '):
                for sequence in word.split(' '):
                    if sequence in table:
                        result += table[sequence]
                    # silently ignore unknown sequences
                result += ' '

            return result.lower()

        def to_bytes(code: str) -> bytes:
            result = b''
            currbyte = 0
            shift = 0
            for char in code:
                if char == '.':
                    currbyte |= (0x01 << shift)
                elif char == '-':
                    currbyte |= (0x02 << shift)
                elif char != ' ':
                    # silently ignore invalid characters
                    continue

                if shift == 6:
                    shift = 0
                    result += currbyte.to_bytes(1, 'big')
                    currbyte = 0
                else:
                    shift += 2
            currbyte |= (0x03 << shift)

            return result + currbyte.to_bytes(1, 'big')

        def from_bytes(data: bytes) -> str:
            result = ''
            for currbyte in data:
                shift = 0
                while shift <= 6:
                    bits = (currbyte >> shift) & 0x03
                    shift += 2
                    if bits == 0x00:
                        result += ' '
                    elif bits == 0x01:
                        result += '.'
                    elif bits == 0x02:
                        result += '-'
                    elif bits == 0x03:
                        return result

            return result

    # p3 is always 0x05
    def getCaps(self) -> Caps:
        return Caps(Caps.COLOR | Caps.RANDOM | Caps.REPEAT | Caps.BRIGHTNESS)

    def resetIds(self) -> None:
        self.settings.setId(BYTE(0x0B))
        self.settings.setParam(3, BYTE(0x05))

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
