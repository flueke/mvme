##################################################
Installation
##################################################

==================================================
System Requirements
==================================================

* Any recent Linux distribution or a version of Windows 7 or later.

  Both 64- and 32-bit systems are supported but the 64-bit version is
  recommended due to the larger address space.

* `WIENER`_ VM-USB VME Controller with a recent firmware

  The VM-USB firmware can be updated from within mvme. See
  :ref:`howto-vmusb-firmware-update` for a guide.

* Latest USB chipset driver for your system.

  Updating the driver is especially important for Windows versions prior to
  Windows 10 in combination with a NEC/Renesas chipset (frequently found in
  laptops). The driver shipped by Microsoft has a bug that prevents libusb from
  properly accessing devices. See the `libusb wiki`_ for more information.

* USB Driver: libusb-0.1 (Linux) / libusb-win32 (Windows)

  The windows installer can optionally run `Zadig`_ to handle the driver
  installation.

* At least 4 GB RAM is recommended.

* A multicore processor is recommended as mvme itself can make use of multiple
  cores: readout, analysis and GUI (which includes histogram rendering) run in
  separate threads.

.. _WIENER: http://www.wiener-d.com/

.. _libusb wiki: https://github.com/libusb/libusb/wiki/Windows

==================================================
Linux
==================================================

The mvme archives for Linux include all required libraries. The only
external dependency is the GNU C Library glibc. When using a modern Linux
distribution no glibc version errors should occur.

Installation is simple: unpack the supplied archive and execute the *mvme*
binary::

    $ tar xf mvme-x64-1.0.tar.bz2
    $ ./mvme-x64-1.0/mvme

VM-USB Device Permissions
--------------------------------------------------

To be able to use the VM-USB controller as a non-root user a udev rule to
adjust the device permissions needs to be added to the system.

Create a file called ``/etc/udev/rules.d/999-wiener-vm_usb.rules`` with the
following contents: ::

    # WIENER VM_USB
    SUBSYSTEM=="usb", ATTRS{idVendor}=="16dc", ATTRS{idProduct}=="000b", MODE="0666"

This will make the VM-USB usable by *any* user of the system. A more secure
version would be: ::

    # WIENER VM_USB
    SUBSYSTEM=="usb", ATTRS{idVendor}=="16dc", ATTRS{idProduct}=="000b", MODE="0660", GROUP="usb"

which requires the user to be a member of the *usb* group.

Reload udev using ``service udev reload`` or ``/etc/init.d/udev reload`` or
``service systemd-udev reload`` depending on your distribution or simply reboot
the machine.


==================================================
Windows
==================================================

Run the supplied installer and follow the on screen instructions to install
mvme.

At the end of the installation process you are given the option to run `Zadig`_
to install the driver required for VM-USB support to work. Refer to the
description text in the installer and :ref:`inst-windows-vmusb-driver` for
details.

.. _inst-windows-vmusb-driver:

VM-USB Driver Installation
--------------------------------------------------

To be able to use the VM-USB VME Controller the *libusb-win32* driver needs to
be installed and registered with the device. An easy way to install the driver
is to use the `Zadig USB Driver Installer <http://zadig.akeo.ie/>`_ which comes
bundled with mvme. You can run Zadig at the end of the installation process or
at a later time from the mvme installation directory.

In the Zadig UI the VM-USB will appear as *VM-USB VME CRATE CONTROLLER*. If it
does not show up there's either a hardware issue or another driver is already
registered to handle the VM-USB. Use *Options -> List All Devices* to get a
list of all USB devices and look for the controller again.

.. _installation-zadig:

.. autofigure:: images/installation_zadig.png
   :scale-latex: 60%

   Zadig with VM-USB and libusb-win32 selected

Make sure *libusb-win32* is selected as the driver to install, then click on
*Install Driver*. `Zadig`_ will generate a self-signed certificate for the
driver and start the installation process.

It is highly recommended to restart your system after driver installation,
especially if you replaced an existing driver. Otherwise USB transfer errors
can occur during VME data acquisition!

In case you want to manually install the driver a ZIP archive can be found
here: `libusb-win32`_.

.. _Zadig: http://zadig.akeo.ie/

.. _libusb-win32: https://sourceforge.net/projects/libusb-win32/files/libusb-win32-releases/1.2.6.0/

.. vim:ft=rst
