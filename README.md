# qtools

This code is originally from https://github.com/forth32/qtools
I simply ran most of the strings and comments through google translate to make usage easier without needing to refer to the source code all of the time

A set of tools for working with flash modems on the Qualcom chipset
The kit consists of a package of utilities and a set of patched bootloaders.

qcommand - an interactive terminal for entering commands through the command port. It replaces the terribly inconvenient revskills.
           It allows you to enter command-byte command packets, edit memory, read and view any flash sector.

qrmem is a program for reading a dump of the modem address space.

qrflash is a flash reader. Able to read both a range of blocks and sections on a partition map.

qwflash is a program for recording partition images through the user partitions mode of the bootloader, similar to QPST.

qwdirect - a program for direct recording of flash drive blocks with / without OOB through the controller ports (without the participation of the bootloader logic).

qdload is a program for loading bootloaders. Requires the modem to be in download mode or PBL emergency boot mode.

dload.sh - a script to put the modem into boot mode and send the specified bootloader to it.

Programs require modified versions of bootloaders. They are collected in the loaders directory, and the patch source
lies in cmd_05_write_patched.asm.