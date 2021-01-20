# Updated `sysv` Module for Linux

The files here have been updated to work on at least Ubuntu 18.04.
To build it, you need the kernel headers. On my system, they're in
`/usr/src/linux-headers-5.4.0-56-generic`. To build the module, do:

    make -f Makefile.daveb KSRC=/usr/src/linux-headers-5.4.0-56-generic/ external

Check that there isn't a `sysv` module active:

    lsmod | grep sysv

If there is, this can be removed with:

    sudo rmmod sysv

Insert your newly-compiled module:

    sudo insmod ./sysv.ko

Export a filesystem image from a volume image:

    s4export -i hd.img -o hd.fs

Now you can mount the image:

    sudo mount -o loop -t sysv hd.fs /mnt

Copy any files you want to the right place, and/or copy files off
of the filesystem. You will likely need to do this via `sudo`.
When done, unmount the filesystem:

    sudo umount /mnt

Use `s4import` to pull the modified filesystem back in to the
volume image:

    s4import -i hd.fs -o hd.img

#### Last Modified:
Tue 19 Jan 2021 11:38:21 PM EST
