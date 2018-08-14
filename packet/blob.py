from .types import Number, BYTE, WORD, DWORD, BYTESTR
from debug import validate_type


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

    def length(self) -> int:
        return self.size

    def remain(self) -> int:
        return 64 - self.offset

    def size(self) -> int:
        return self.size

    def dump(self) -> None:
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

    def readBytes(self, numBytes) -> bytes:
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

    def readByte(self) -> BYTE:
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

        return BYTE(self.readBytes(1))

    def readWord(self) -> WORD:
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

        return WORD(self.readBytes(2))

    def readDword(self) -> DWORD:
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

        return DWORD(self.readBytes(4))

    def writeBytes(self, byteStr) -> int:
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

        if not isinstance(byteStr, bytes):
            validate_type(byteStr, [Number, BYTESTR])
            byteStr = bytes(byteStr)

        c = len(byteStr)
        assert self.size + \
            c < 64, 'Not enough remaining space (%d bytes) to write (%d bytes)' % (
                64 - self.size, c)

        self.data += byteStr
        self.size += c
        self.offset += c

        return c
