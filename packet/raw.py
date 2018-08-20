from .blob import Blob, BYTE
from debug import validate


class Raw:
    """Represents a raw data packet (16 DWORDS).

    This is only used during development.

    Parameters
    ----------
    byteStr : bytes
        The raw data read from the USB.

    """

    mode = None
    operation = None

    def __init__(self, mode: int = None, operation: int = None, index: int = None, flags: int = None,
                 data: bytes = None, padChar: bytes = b'\x00'):
        self.padChar = padChar
        self.padding = b''

        if data is not None and len(data) >= 4:
            if mode is None:
                mode = data[0]
            if operation is None:
                operation = data[1]
            if index is None:
                index = data[2]
            if flags is None:
                flags = data[3]

        assert mode is not None, "Expected 'mode' to be configured"
        assert operation is not None, "Expected 'operation' to be configured"
        assert index is not None, "Expected 'index' to be configured"
        assert flags is not None, "Expected 'flags' to be configured"

        self.mode = BYTE(mode)
        self.operation = BYTE(operation)
        self.index = BYTE(index)
        self.flags = BYTE(flags)

        if data is not None:
            self.update(data)

    def setMode(self, mode: int) -> None:
        self.mode = BYTE(mode)

    def readHeader(self, blob: Blob) -> None:
        if self.mode is not None:
            validate(blob.readByte(), self.mode, 'Mode missmatch - expected $1, got $0')
        else:
            self.mode = blob.readByte()

        if self.operation is not None:
            validate(blob.readByte(), self.operation,
                     'Operation missmatch - expected $1, got $0')
        else:
            self.operation = blob.readByte()

        if self.index is not None:
            validate(blob.readByte(), self.index, 'Index missmatch - expected $1, got $0')
        else:
            self.index = blob.readByte()

        if self.flags is not None:
            validate(blob.readByte(), self.flags, 'Flags missmatch - expected $1, got $0')
        else:
            self.flags = blob.readByte()

    def encode(self, blob: Blob) -> None:
        return

    def decode(self, blob: Blob) -> None:
        """Reads the remaining 62 bytes of a 64 byte packet.

           Derived classes should implement this to populate fields.

        Parameters
        ----------
        blob : Blob
            Description of parameter `blob`.

        Raises
        -------
        AssertionError
            If trying to read more than 64 bytes.

        """
        c = 0
        while c < 15:
            setattr(self, "dword%02d" % c, blob.readDword())
            c += 1

    def to_blob(self) -> Blob:
        blob = Blob(debug=False, padChar=self.padChar)
        blob.writeBytes(self.mode)
        blob.writeBytes(self.operation)
        blob.writeBytes(self.index)
        blob.writeBytes(self.flags)
        # derived classes should implement this
        self.encode(blob)
        if len(self.padding) > 0:
            blob.writeBytes(self.padding)
        return blob

    def dump(self):
        self.to_blob().dump()

    def update(self, byteStr: bytes):
        blob = Blob(byteStr, padChar=self.padChar)
        try:
            self.readHeader(blob)
            self.decode(blob)
            if blob.remain() > 0:
                self.padding = blob.readBytes(blob.remain())
        except AssertionError as error:
            print(error)
            blob.debug = False  # suppress the __del__ error checking
            blob.dump()
            return ErrorResponse(byteStr)
        return self

    def displayName(self):
        return 'Packet.Raw'

    def __bytes__(self):
        return bytes(self.to_blob())

    def __getitem__(self, i):
        return getattr(self, list(self.__dict__)[i])

    def __len__(self):
        return len(self.__dict__)

    def __repr__(self):
        # str = 'Packet.%s(' % self.__class__.__name__
        str = '%s(' % self.displayName()
        for field in self.__dict__:
            if field in ['padChar', 'padding']:
                continue
            str += "\n    %s: %s" % (field, getattr(self, field))
        return str + "\n)"


class ErrorResponse(Raw):
    def __init__(self, data: bytes):
        super().__init__(data=data)

    def displayName(self):
        return 'Packet.ErrorResponse'

    def decode(self, blob: Blob) -> None:
        self.prev_mode = blob.readByte()
        self.prev_operation = blob.readByte()
        self.prev_index = blob.readByte()
        self.prev_flags = blob.readByte()
