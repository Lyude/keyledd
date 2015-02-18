What is this?
=============

keyledd is a very simple and small daemon for using a system LED on Linux to represent one of the LEDs on a keyboard, such as the Caps Lock LED or the Number Lock LED. keyledd will connect to an evdev input device, and change the brightness value of an LED on the system to a user specified value every time the specified keyboard LED changes. Every evdev keyboard device has a Scroll Lock, Caps Lock, and Number Lock LED regardless of whether or not the physical keyboard actually has one. Because of this, keyledd can be used to compensate for one of these LEDs missing. I originally wrote this so that I could use the FnLock LED on my ThinkPad T440s as a caps lock LED.

How do I use it?
================

Right now, there's no packages for this, nor is there a make install option (but one will be on the way!). The simplest way to use it is to compile the application, and simply run it from the `src/` directory. All of the options necessary to configure keyledd can be found by running:

``
./src/keyledd -h
``

Eventually when I get the chance, I will make a proper install option in the makefile, along with adding the ability to run as a daemon, and parse a configuration file in `/etc`.

How to compile keyledd
======================

Go into the directory you've downloaded the source code to, and run:

``
./autogen.sh
make
``

Dependencies
============

* libevdev
* glib-2.0
