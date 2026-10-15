Serial bootloader server for TRS-80 Model 4 **Portables**. Original code by George Phillips, located at https://48k.ca/boot4p.html

## Current status
**UNTESTED**

I got it to compile but have not tested it. I don't actually own a 4p, so it will be difficult to test.

In order to test for yourself:

 0. Install zmac assembler, or if you would prefer not to, comment out the rule in the `Makefile` that uses it. The product of zmac running on `boot2.z` is put into `boot4p.h` anyway, which is already included in this repo.
 1. on Linux and Mac, modify the `listcom()` function in `listcom.cpp` to give the tty number you would prefer. It will be used to open the device `/dev/tty.usbmodem{n}`, so Linux users may want to also change this string in `boot4p.cpp`.

Use at your own risk. If you get it working, please submit patches or at the very least put an issue in github to tell me it works.