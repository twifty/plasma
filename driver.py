import usb.core
from packet import Packet


class Driver:
    def __init__(self, vendorId, productId, interface=1):
        self.vendorId = vendorId
        self.productId = productId
        self.interfaceNum = interface
        self.device = None

    def __del__(self):
        self.detach()

    def attach(self):
        self.device = usb.core.find(idVendor=self.vendorId, idProduct=self.productId)

        if self.device is None:
            raise RuntimeError("Failed to find device %s:%s" % (self.vendorId, self.productId))

        # for configuration in self.device:
        #     for interface in configuration:
        #         if self.device.is_kernel_driver_active(interface.bInterfaceNumber):
        #             # try:
        #                 self.device.detach_kernel_driver(interface.bInterfaceNumber)
        #             #     print("kernel driver detached from %d" % interface.bInterfaceNumber)
        #             # except usb.core.USBError as e:
        #             #     sys.exit("Could not detach kernel driver: %s" % str(e))
        #         # else:
        #         #     print("no kernel driver attached to interface: %d" % interface.bInterfaceNumber)

        configuration = self.device.get_active_configuration()
        if configuration is None or configuration.bConfigurationValue != 1:
            self.device.set_configuration(1)
        # device.set_configuration()

        # get an endpoint instance
        configuration = self.device.get_active_configuration()
        if configuration is None:
            raise RuntimeError("Failed to find a valid configuration.")
        # print(cfg)

        self.interface = configuration[(self.interfaceNum, 0)]

        if self.device.is_kernel_driver_active(self.interface.bInterfaceNumber):
            self.device.detach_kernel_driver(self.interface.bInterfaceNumber)

        usb.util.claim_interface(self.device, self.interface)

        self.reader = self.interface[0]
        self.writer = self.interface[1]

    def detach(self):
        if self.device is not None:
            usb.util.release_interface(self.device, self.interface)
            self.device.attach_kernel_driver(self.interface.bInterfaceNumber)

            self.reader = None
            self.writer = None
            self.interface = None
            self.device = None

    def post(self, request, timeout=100):
        if self.device is None:
            self.attach()

        assert isinstance(request, Packet.Raw), "Expected a Packet, got %s" % type(request)

        written = self.writer.write(bytes(request), timeout)
        if written is not 64:
            raise RuntimeError("Incorrect number of bytes written: %d" % written)

        response = self.reader.read(64, timeout)
        if len(response) is not 64:
            raise IOError("Expected a 64 byte response, got %d" % len(response))

        return request.update(bytes(response))
