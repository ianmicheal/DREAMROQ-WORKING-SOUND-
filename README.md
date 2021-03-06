# DREAMROQ-WORKING-SOUND-
Update working sound on  kos1.3
29fps
==============
Ian micheal update fix request no black screen working sound 
================================================================
Geting any thing to sync will be hard it all has to be done with encoding

this will try but will run it 29fps flat out all the time 
Included batch files to convert mp4 or avi that has been converted to 512x512 to ROQ with sound that works with this player
this is a sample player it can be used for fmv in your game and as an intro lib to show your fmv intro then boot your main
game etc.. 

request filled

Files must be
=====================
512x512 22khz before using the encoder and batches files use shana encoder it's free
mp4 or avi batch files are included.. you cant use any size other then 512x512 in and out..

License
=======
Dreamroq is meant to be license-compatible with the rest of the KallistiOS
operating system, which is a BSD-style open source license. You can read
the specific text in LICENSE.KOS.

So you can use it in your games not have problems.

Dreamroq Library

Introduction
============
Dreamroq is a RoQ playback library designed for the Sega Dreamcast video
game console.

RoQ is a relatively simple video file format developed for video-heavy
CD-ROM games. Read more about the format here:

http://wiki.multimedia.cx/index.php?title=RoQ

The Dreamroq library includes a player component that is designed to run
under the KallistiOS (KOS) open source operating system. Read more about
KOS at:

http://gamedev.allusion.net/softprj/kos/

The library also includes a sample testing utility that can be built
and executed on Unix systems. This utility is useful for debugging and
validation.

RoQ sample files can be found at:

http://samples.mplayerhq.hu/game-formats/idroq/

RoQ files can also be created using the Switchblade encoder:

http://icculus.org/~riot/

A version of Switchblade is also included in FFmpeg and many derivative
programs:

http://ffmpeg.org/


License
=======
Dreamroq is meant to be license-compatible with the rest of the KallistiOS
operating system, which is a BSD-style open source license. You can read
the specific text in LICENSE.KOS.


Building (Unix)
===============
To build and test on Linux/Mac OS X/Cygwin, simply type:

  make

in the source directory. This will build the executable test-dreamroq. This
utility has the following usage:

  ./test-dreamroq <file.roq>

This will decode the RoQ file from the command line into a series of PNM
files in the current working directory (watch out-- this could take up a
lot of disk space).


Building (KOS)
==============
There are 2 Makefiles included with Dreamroq. The first -- implicitly
invoked when running a bare 'make' command as seen in the "Building (Unix)"
section -- builds the test utility. The second Makefile is Makefile.KOS,
invoked with:

  make -f Makefile.KOS

This is a standard KOS Makefile which assumes that a KOS build environment
is available. This is covered in the KOS documentation. This step will
build a file named dreamroq-player.elf which can be loaded onto a Dreamcast
console via standard means (also outside the scope of this document).

The file dreamcast-player.c contains a hardcoded RoQ file path in its
main function. It is best if this points to a file burned on an optical
disc. It is also viable to build a small RoQ file as a ROM disk into the
ELF file (which is well supported in KOS) and load the file from the '/rd'
mount point.


Bugs, Issues, and Future Development
====================================
The player is just a proof of concept at this point. It doesn't try to
render frames with proper timing-- it just plays them as fast as possible
(which often isn't very fast, mostly due to the I/O bottleneck).

If the RoQ video is taller than it is wide, expect some odd rendering.

The API between the library and the client app leaves a bit to be desired.
Notably absent is some proper initialization and teardown mechanism.
Currently, the first call to the render callback initializes the PVR buffers
but they are never explicitly freed.


Credits
======= 
Library originally written by Mike Melanson (mike -at- multimedia.cx)

Audio support added by Josh "PH3NOM" Pearson (ph3nom.dcmc@gmail.com)
