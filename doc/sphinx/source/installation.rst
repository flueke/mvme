==================================================
Installation
==================================================
.. warning::
    TODO: Write this

Linux
--------------------------------------------------

The **mvme** archives for Linux include all required libraries. The only
external dependency is the GNU C Library glibc. When using a modern Linux
distribution no glibc version errors should occur.

Installation is simple: unpack the supplied archive and execute the *mvme*
binary::

    $ tar xf mvme-x64-1.0.tar.bz2
    $ ./mvme-x64-1.0/mvme

VM-USB Device Permissions
~~~~~~~~~~~~~~~~~~~~~~~~~

To be able to use the VM-USB controller as a non-root user a udev rule to
adjust the devices permissions needs to be added to the system.

Create a file called ``/etc/udev/rules.d/999-wiener-vm_usb.rules`` with the following contents: ::

    # WIENER VM_USB
    SUBSYSTEM=="usb", ATTRS{idVendor}=="16dc", ATTRS{idProduct}=="000b", MODE="0666"

This will make the VM-USB useable by *any* user of the system. A more secure version would be: ::

    # WIENER VM_USB
    SUBSYSTEM=="usb", ATTRS{idVendor}=="16dc", ATTRS{idProduct}=="000b", MODE="0660", GROUP="usb"

which requires the user to be a member of the *usb* group.

Reload udev using ``service udev reload`` or ``/etc/init.d/udev reload``
depending on your distribution.


Windows
--------------------------------------------------

Run the supplied installer to install **mvme**. All required libraries are
included in the installer.

After the installation process you are given the option to run Zadig to install
the drivers required for VM-USB support to work. Refer to the description text
in the installer and :ref:`inst-windows-vmusb-driver` for details.

.. _inst-windows-vmusb-driver:

VM-USB Driver Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _installation-zadig:

.. figure:: images/installation_zadig.png
   :width: 12cm

   Zadig USB driver installer

http://zadig.akeo.ie/

https://sourceforge.net/projects/libusb-win32/files/libusb-win32-releases/1.2.6.0/
