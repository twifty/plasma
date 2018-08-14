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

    def __init__(self, mode=None, operation=None, data=None, padChar=b'\x00'):
        self.padChar = padChar
        self.padding = b''
        if data is None:
            assert mode is not None, "Expected 'mode' to be configured"
            assert operation is not None, "Expected 'operation' to be configured"
            self.mode = BYTE(mode)
            self.operation = BYTE(operation)
        else:
            self.update(data)

    def _setOperation(self, mode, operation):
        """Sets or validates the mode and operation bytes.

        Parameters
        ----------
        mode : byte
            The first byte in a packet.
        operation : byte
            The second byte in a packet.

        Raises
        -------
        AssertionError
            If the bytes don't match the previously configured.

        """
        if self.mode is not None:
            validate(mode, self.mode, 'Mode missmatch - expected %02X, got %02X')
        else:
            self.mode = mode

        if self.operation is not None:
            validate(operation, self.operation, 'Operation missmatch - expected %02X, got %02X')
        else:
            self.operation = operation

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
        # derived classes should implement this
        self.encode(blob)
        if len(self.padding) > 0:
            blob.writeBytes(self.padding)
        return blob

    def dump(self):
        self.to_blob().dump()

    def update(self, byteStr: bytes):
        blob = Blob(byteStr, padChar=self.padChar)
        self._setOperation(blob.readByte(), blob.readByte())
        self.decode(blob)
        self.padding = blob.readBytes(blob.remain())

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
            if field == 'padChar':
                continue
            str += "\n    %s: %s" % (field, getattr(self, field))
        return str + "\n)"
