What is this?
=============

keyledd is a very simple and small daemon for using a system LED on Linux to represent one of the LEDs on a keyboard, such as the Caps Lock LED or the Number Lock LED. keyledd will connect to an evdev input device, and change the brightness value of an LED on the system to a user specified value every time the specified keyboard LED changes. Every evdev keyboard device has a Scroll Lock, Caps Lock, and Number Lock LED regardless of whether or not the physical keyboard actually has one. Because of this, keyledd can be used to compensate for one of these LEDs missing. I originally wrote this so that I could use the FnLock LED on my ThinkPad T440s as a caps lock LED.

How do I use it?
================

By default, keyledd reads from the configuration file `/etc/keyledd.conf`. Within this file, you specify each input device you want monitored, along with the key LED you want monitored on each device. For example, on my T440s, I use this:

```INI
[T440s Caps Lock] # The group name. This can be whatever you want.
KeyboardLed=capslock # Can be "capslock", "numlock", or "scrolllock"
InputDevice=/dev/input/by-path/platform-i8042-serio-0-event-kbd # The evdev input device you'd like to monitor, in this case the built-in keyboard
LedDevice=/sys/class/leds/tpacpi::unknown_led # The LED you'd like to act as the indicator, on the T440s the first unknown_led corresponds to the Fn lock key
BrightnessOn=1 # The brightness value to use when the Keyboard LED is on (optional, defaults to 1)
BrightnessOff=0 # The brightness value to use when the Keyboard LED is off (optional, defaults to 0)
```

You can specify as many LEDs as you wish. Just use the systemd unit to start the daemon on boot, and it will do the rest for you!

How to compile keyledd
======================

Go into the directory you've downloaded the source code to, and run:

``
./autogen.sh
make
make install
``

Dependencies
============

* libevdev
* glib-2.0
* gio-2.0
