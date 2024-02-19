#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/

KERNEL_PATH=/opt/Bede/Bede-linux
ROOTFS_PATH=${BASEDIR}

QEMU_SYSTEM_BINARY=`which qemu-system-x86_64`
BZIMAGE_PATH=${KERNEL_PATH}/arch/x86_64/boot/bzImage
INITRD_PATH=/boot/initrd.img-6.4.0+
IMAGE_PATH=/opt/qemu-image.img

    # -smp 4 -S -s \
 ${QEMU_SYSTEM_BINARY} \
     -smp 1 --enable-kvm -cpu host \
    -numa node,cpus=0,memdev=mem0,nodeid=0 \
    -object memory-backend-ram,id=mem0,size=4G \
	 -m 4G,slots=1,maxmem=6G \
    -kernel ${BZIMAGE_PATH} -nographic  -append "root=/dev/sda rw console=ttyS0" \
    -drive file=${IMAGE_PATH},index=0,media=disk,format=raw \
    -serial mon:stdio  -monitor telnet:127.0.0.1:1234,server,nowait -nic bridge,br=virbr0,model=virtio-net-pci,mac=02:76:7d:d7:1e:3f
