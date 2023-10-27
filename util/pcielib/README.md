# pcielib

This subdirectory contains a thin wrapper for interacting with PCI Express peripherals on seL4. Please note that this library only supplies an interface - all the real work is done by the seL4 kernel at boot time.

## Limitations

This driver piggybacks off of the seL4 kernel's walk of the PCI configuration space. This means that it will likely behave poorly on x86_64 systems that have:

* Too many PCI-E devices
* More than one root PCI-E node (multi-domain systems)
* Devices which will cause an address-space collision when being mapped from UEFI - address determination occurs at runtime and behaviour will be unpredictable

**This library is only intended to work on PC99 architectures!** The base-address register interfacing likely can be moved to ARM however with minimal work.

