mvme2 test version
==================

libusb-win32 is needed for VM_USB connectivity. The driver is included in the
archive. To install the driver run libusb-win32-bin-1.2.6.0\bin\inf-wizard.exe
to create and install the filter driver for your VM_USB. The VM_USB should now
show up under "libusb-win32 devices" in the windows device manager.

For the software to work the VM_USB needs to be plugged in at startup.
Otherwise the control panel won't appear and no datataking is possible.

Right now mvme2 is limited to one VME DAQ module at base address 0x0000.
Modules using other addresses can be written to and read from but datataking
is currently hard-coded to work with address 0x0000.