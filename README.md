Fork
====
This is a fork of https://github.com/dgesswein/s4-3b1-pc7300,
which in turn
is a fork of https://github.com/dad4x/s4-3b1-pc7300, with fixes
to work with his images of 3b1 disk drives.

So far this fork has updated the `sysv` driver to work on more
modern Linux systems, in particular on Ubuntu 18.04.  I (Arnold Robbins)
have added a README.md file there with more information.

NOTE: There is a disconnect between Unix and the `sysv` driver. After
exporting a filesystem image from a volume image, you must run `s4fsck`
on it to rebuild the free list. Otherwise, Linux will think there's
no free space.  After importing such a filesystem, Unix runs a longer
`fsck` than usual, but things work OK.  I have not tried to track down
the problem; help with this would be welcome.

Note also that when creating files on a mounted System V filesystem,
Linux will *not* truncate file names for you; you must keep
filenames to 14 characters or less.

Purpose
=======

Access to AT&T UNIX-PC 3B1 / PC-7300 / "Safari 4" tools, especially for disks, disk images, and filesystems.

  * libs4, library for access to volume image files and file systems
  * various command line tools using libs4
  * Modified linux sysv filesystem that can mount file system images
  
Volume images can be used with the Phil Pemberton FREEBEE 3b1 emulator directly, or
as disks on a real machine when using the Gesswein/PDP [MFM emulator board]www.pdp8online.com/mfm

Release Status
==============

Sub-alpha as of initial commit (1/2015).  No guarantees, always work
with copies!  Images of volumes and file systems for the 3b1 are small
enough by current standards that you should be expansive in your use
of copies.  A full-sized file volume image of 90M is not many seconds
of cat video.


LIBS4
-----

A header and library for opening files containing volumes, and poking
around the filesystem that may be contained there.  The header is
`<s4d.h>`.


TOOLS
-----

  * s4date        a date(1) that starts in 2000 instead of 1900, from SVR2 source
  * s4disk        tool to inspect volume images, similar to `iv -t` on the real machine
  * s4dump        a hex/ascii dumper tuned for dumping vol and FS files.
  * s4export      export filesystem from volume to mountable FS image file
  * s4fs          a tool for exploring FS image files (not very deeply)
  * s4fsck        SVR2 FSCK modified to work on possibly byte-swapped FS file
  * s4import      merge a FS file into a volume image.
  * s4merge       tool for merging multiple extracted volume images into one, block by block.
  * s4mkfs        SVR2 MKFS modified to generate a file and handle byte-swapping
  * s4test        whatever little test was needed most recently
  * s4vol         a tool for deeper futzing with volume files.
    
SYSV
----

Modifications to linux to allow mounting of native file system images using

  `mount -t sysv -o loop` *filesystem-image-file* `/mnt/someplace`

merging this to your local linux is out of my current comprehension, but I've gotten it to work with a Debian and a Suse.
