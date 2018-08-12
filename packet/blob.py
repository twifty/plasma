from struct import unpack


class Resolution:
    """Helper class for converting 4byte numbers to 3 bytes.

       An instance of this class stores the number of clock ticks between LED power cycles.

       The tick timer of an LED is calculated as 48kHz/frequency. Usually the higher
       the frequency, the dimmer the LED. CoolerMaster must keep the LED powered on for
       a hard coded number of ticks. The more cycles, the brighter the LED.

       Since only 3 bytes are available for each resolution, the number is increasingly
       diveded until it fits within 2 bytes, then the number of divisions is stored in the
       first byte. This results in an unnoticable decrease of the true resolution.
    """
    Max = 1066666
    Min = 24000

    def round(value):
        if isinstance(value, float):
            value = int(value)
        else:
            assert isinstance(value, int), "Can only encode int values, got %s" % type(value)
        assert value <= Resolution.Max, "Value %d exceeds max of %d" % (
            value, Resolution.Max)
        assert value >= Resolution.Min, "Value %d subceeds min of %d" % (
            value, Resolution.Max)
        return value

    def decode(value):
        assert isinstance(value, bytes), "Expected a bytes array, got %s" % type(value)
        assert len(value) == 3, "Expected 3 bytes, got %d" % len(value)

        (hi, lo) = unpack('BH', value)

        if hi > 0:
            lo += 1
            lo *= hi + 1

        return Resolution.round(lo)

    def encode(value):
        value = Resolution.round(value)
        hi = 0
        lo = value

        while lo > 0xFFFF:
            hi += 1
            lo = int(value / (hi + 1))

        lo -= 1

        return hi.to_bytes(1, 'little') + lo.to_bytes(2, 'little')

    def __init__(self, value):
        if isinstance(value, bytes):
            self.value = Resolution.decode(value)
        else:
            self.value = Resolution.round(value)

    def __bytes__(self):
        return Resolution.encode(self.value)

    def __str__(self):
        return str(self.value)


class RGB():
    """Storage unit for 3 bytes representing a color.

    Parameters
    ----------
    r : int | bytes
        The full RGB byt value or an integer ranging 0-255 for the red color.
    g : int
        an integer ranging 0-255 for the green color.
    b : int
        an integer ranging 0-255 for the blue color.

    """

    def __init__(self, r, g=None, b=None):
        if isinstance(r, int):
            assert isinstance(g, int), "Expected an int, got %s" % type(g)
            assert isinstance(b, int), "Expected an int, got %s" % type(b)
            assert r >= 0x00 and r <= 0xFF, "Colors values must be in the range 0-255, got %d." % r
            assert g >= 0x00 and r <= 0xFF, "Colors values must be in the range 0-255, got %d." % g
            assert b >= 0x00 and r <= 0xFF, "Colors values must be in the range 0-255, got %d." % b
            self.value = r.to_bytes(1, 'little') + g.to_bytes(1, 'little') + b.to_bytes(1, 'little')
        elif isinstance(r, bytes):
            assert len(r) == 3, "Expected exactly 3 bytes, got %d." % len(r)
            self.value = r
        else:
            raise AssertionError("Expected ints or a bytes string, got %s." % type(r))

    def __str__(self):
        return "Red = 0x%02X, Green = 0x%02X, Blue = 0x%02X" % (self.value[:1], self.value[1:2], self.value[2:])

    def __bytes__(self):
        return self.value


class Number(int):
    """Helper class for displaying numbers in their hexadecimal format.

    Parameters
    ----------
    byteStr : bytes
        A number encoded in a byte string.

    Attributes
    ----------
    size : int
        The size of the number (1, 2 or 4 bytes).
    value : int
        The integer value of the number.

    """

    def __new__(self, byteStr, size=None):
        if size is None:
            assert isinstance(byteStr, bytes), "Expected a byte string, got %s" % type(byteStr)
            size = len(byteStr)
            if size is 1:
                value = ord(byteStr)
            elif size is 2:
                value = unpack('H', byteStr)[0]
            elif size is 4:
                value = unpack('I', byteStr)[0]
            else:
                raise RuntimeError('Invalid byte length: %d' % size)
        else:
            assert isinstance(byteStr, int), "Expected an int, got %s" % type(byteStr)
            if size is 1:
                value = byteStr & 0xff
            elif size is 2:
                value = byteStr & 0xffff
            elif size is 4:
                value = byteStr
            else:
                raise RuntimeError('Invalid size: %d' % size)

        inst = super(Number, self).__new__(self, value)

        inst.value = value
        inst.size = size

        return inst

    # def __int__(self):
    #     return self.value

    def __str__(self):
        if self.size is 1:
            return "0x%02X" % self.value
        if self.size is 2:
            return "0x%04X" % self.value
        if self.size is 4:
            return "0x%08X" % self.value

    def __bytes__(self):
        return self.value.to_bytes(self.size, 'little')


class DWORD(Number):
    def __new__(self, value):
        return super(DWORD, self).__new__(self, value, 4)


class WORD(Number):
    def __new__(self, value):
        return super(WORD, self).__new__(self, value, 2)


class BYTE(Number):
    def __new__(self, value):
        return super(BYTE, self).__new__(self, value, 1)


class Blob:
    """Helper class for reading out raw values.

    Parameters
    ----------
    byteStr : bytes
        64 bytes of raw packet data.

    """

    def __init__(self, byteStr=None, padChar=None, debug=True):
        if byteStr is not None:
            assert isinstance(byteStr, bytes), 'Expected 64 bytes, got %s.' % type(byteStr)
            assert len(byteStr) is 64, 'Expected 64 bytes, got %s.' % len(byteStr)

            self.data = byteStr
            self.size = 64
        else:
            self.data = b''
            self.size = 0

        self.debug = debug
        self.offset = 0
        self.padChar = padChar

    def __del__(self):
        if self.debug:
            assert self.offset == 64, 'Not all bytes read/written, %d remain.' % (64 - self.offset)

    def __bytes__(self):
        return self.data + ((64 - self.size) * self.padChar)

    def dump(self):
        data = bytes(self)
        print("[")
        for x in range(0, 8):
            print("    ", end='')
            for y in range(0, 8):
                i = x * 8 + y
                print("0x%02X" % data[i], end='')
                if i is not 63:
                    print(', ', end='')
            print("")
        print("]")

    def readBytes(self, numBytes):
        """Reads and returns numBytes of raw data.

        Parameters
        ----------
        numBytes : int
            The number of bytes to read.

        Returns
        -------
        bytes
            The unparsed data.

        Raises
        -------
        AssertationException
            If trying to read outside the range of available data.

        """

        end = self.offset + numBytes

        assert numBytes > 0 and numBytes < 64, 'numBytes must be within the range 0-63, got %d.' % numBytes
        assert end <= 64, 'unable to read %d bytes, only %d remain.' % (numBytes, 64 - self.offset)

        str = self.data[self.offset:end]
        self.offset = end

        return str

    def readByte(self, expect=None):
        """Reads a single character (1 byte).

        Returns
        -------
        int
            The read data.

        Raises
        -------
        AssertationException
            If trying to read outside the range of available data.

        """

        num = Number(self.readBytes(1))
        if expect is not None:
            assert expect == num, "Expected 0x%X, got %s" % (expect, num)
        return num

    def readWord(self, expect=None):
        """Reads a single word (2 bytes).

        Returns
        -------
        int
            The read data.

        Raises
        -------
        AssertationException
            If trying to read outside the range of available data.

        """

        num = Number(self.readBytes(2))
        if expect is not None:
            assert expect == num, "Expected 0x%X, got %s" % (expect, num)
        return num

    def readDword(self, expect=None):
        """Reads a single dword (4 bytes).

        Returns
        -------
        int
            The read data.

        Raises
        -------
        AssertationError
            If trying to read outside the range of available data.

        """

        num = Number(self.readBytes(4))
        if expect is not None:
            assert expect == num, "Expected 0x%X, got %s" % (expect, num)
        return num

    def writeBytes(self, byteStr):
        """Appends the bytes to the buffer.

        Parameters
        ----------
        byteStr : bytes|Number
            The data to append.

        Raises
        -------
        AssertationError
            If the given data is either to big or of the wrong type.

        """

        _type = type(byteStr)
        if isinstance(byteStr, Number) or _type == RGB or _type == Resolution:
            byteStr = bytes(byteStr)
        elif _type != bytes:
            raise AssertionError(
                'Expected one of [bytes, Number, RGB, Resolution], got %s.' % _type)

        c = len(byteStr)
        assert self.size + \
            c < 64, 'Not enough remaining space (%d bytes) to write (%d bytes)' % (
                64 - self.size, c)

        self.data += byteStr
        self.size += c
        self.offset += c
