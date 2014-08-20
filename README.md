g_hid-demo
==========

Quick hack to demo g_hid functionality on BeagleBone Black

This is a quick and dirty hack to demonstrate the gadget hid driver.
Normally the platform_device descriptor would be part of the board
support for the device, e.g. in arch/arm/mach-omap2/devices.c,
but for convenience I am putting it in to the g_hid module to make
easier for testing.

Copy this file over the one in drivers/usb/gadget, build
the g_hid module and copy to the device.

Load it on the BeagleBone using modprobe:

modprobe g_hid

Note: if using the default Angstrom build you will have to disable
the g_multi gadget driver that is loaded by default. Do that by
chmod -x /usr/bin/g-ether-load.sh 
then reboot


