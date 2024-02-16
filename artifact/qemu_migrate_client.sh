#!/bin/bash

readonly BASEDIR=$(readlink -f $(dirname $0))/

source "${BASEDIR}/SMDK/script/common.sh"

# QEMU_PATH=${BASEDIR}/Drywall/
#QEMU_PATH=${BASEDIR}/Drywall/
#QEMU_BUILD_PATH=${QEMU_PATH}/build/
# SMDK_KERNEL_PATH=${BASEDIR}/SMDK/lib/linux-6.0-rc6-smdk/
SMDK_KERNEL_PATH=/opt/Bede/Bede-linux
ROOTFS_PATH=${BASEDIR}
MONITOR_PORT=45454

QEMU_SYSTEM_BINARY=`which qemu-system-x86_64`
BZIMAGE_PATH=${SMDK_KERNEL_PATH}/arch/x86_64/boot/bzImage
INITRD_PATH=/boot/initrd.img-6.4.0+
IMAGE_PATH=/opt/qemu-image.img

function print_usage(){
	echo ""
	echo "Usage:"
	echo " $0 [-x vm_index(0-9)]"
	echo ""
}

while getopts "x:" opt; do
	case "$opt" in
		x)
			if [ $OPTARG -lt 0 ] || [ $OPTARG -gt 9 ]; then
				echo "Error: VM count should be 0-9"
				exit 2
			fi
			VMIDX=$OPTARG
			;;
		*)
			print_usage
			exit 2
			;;
	esac
done

if [ -z ${VMIDX} ]; then
	NET_OPTION="-net user,hostfwd=tcp::2242-:22,hostfwd=tcp::6379-:6379,hostfwd=tcp::11211-:11211, -net nic"
else
	echo "Info: Running VM #${VMIDX}..."
	MONITOR_PORT="4545${VMIDX}"
	IMAGE_PATH=$(echo ${IMAGE_PATH} | sed 's/.img/-'"${VMIDX}"'.img/')
	MACADDR="52:54:00:12:34:${VMIDX}${VMIDX}"
	TAPNAME="tap${VMIDX}"
	NET_OPTION="-net nic,macaddr=${MACADDR} -net tap,ifname=${TAPNAME},script=no"

	IFCONFIG_TAPINFO=`ifconfig | grep ${TAPNAME}`
	if [ -z "${IFCONFIG_TAPINFO}" ]; then
		log_error "${TAPNAME} SHOULD be up for using network in VM. Run 'setup_bridge.sh' in /path/to/SMDK/lib/qemu/"
		exit 2
	fi
fi

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
	log_error "qemu-system-x86_64 binary does not exist. Run 'build_lib.sh qemu' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -f "${BZIMAGE_PATH}" ]; then
	log_error "SMDK kernel image does not exist. Run 'build_lib.sh kernel' in /path/to/SMDK/lib/"
	exit 2
fi

if [ ! -f "${IMAGE_PATH}" ]; then
	log_error "QEMU rootfs ${IMAGE_PATH} does not exist. Run 'create_rootfs.sh' in /path/to/SMDK/lib/qemu/"
	exit 2
fi
    # -smp 4 -S -s \
 ${QEMU_SYSTEM_BINARY} \
     -smp 1 --enable-kvm -cpu host \
    -numa node,cpus=0,memdev=mem0,nodeid=0 \
    -object memory-backend-ram,id=mem0,size=4G \
	 -m 4G,slots=1,maxmem=6G \
    -kernel ${BZIMAGE_PATH} -nographic  -append "root=/dev/sda rw console=ttyS0" \
    -drive file=${IMAGE_PATH},index=0,media=disk,format=raw \
    -serial mon:stdio  -monitor telnet:127.0.0.1:1234,server,nowait
