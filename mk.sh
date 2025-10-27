#!/bin/bash

#============================ changlog ===========================
# v1.0: 2025.03.25 
#       crash8.0.6+, gdb-16.2, static, strip. support --vmap.
#       * Build crash tool statically linked and stripped.
#       * add --vmap args to support vmap stack
#       * add --no_refresh args to fix ARM32 parsing errors.
#       * Add build script mk.sh
# v1.1: 2025.06.12
#       build on ubuntu22, fix for glibc float error
#       * error info: Fatal signal: Floating point exception...__libc_early_init...
# v1.2: 2025.06.20
#       add tools uncompress with parameter --umcompress.
#       * cmd example: --uncompress -b -i ./crashdump-1.bin -o dump
#=================================================================

#============================= env setup =========================
# ubuntu 22.0, compile crash static：
# X86_64bit lib-dev:
# sudo apt update
# sudo apt install binutils-aarch64-linux-gnu gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
# sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi libc6-arm64-cross
# sudo apt install autopoint autoconf automake libtool-bin build-essential bison zlib1g-dev flex
# sudo apt install libgmp-dev libmpfr-dev m4 libtool gettext texinfo gperf groff
# sudo apt install libncurses5-dev libncursesw5-dev
# sudo apt install build-essential libzstd-dev libzstd1 liblzma-dev zlib1g-dev
#
# i386_32bit lib-dev:
# sudo dpkg --add-architecture i386
# sudo apt update
# sudo apt install gcc-multilib g++-multilib binutils-multiarch
# sudo apt install libc6-dev-i386 libstdc++-12-dev:i386 libgcc-12-dev:i386 zlib1g-dev:i386
# sudo apt install libgmp-dev:i386 libmpfr-dev:i386 m4:i386 libtool:i386
# sudo apt install libncurses5-dev:i386 libncursesw5-dev:i386 libtinfo5:i386
# sudo apt install libzstd-dev:i386 liblzma-dev:i386 zlib1g-dev:i386 libelf-dev:i386
#=================================================================

#========================== libs version =========================
#           server:  Ubuntu 24.04.1 LTS， 5.15.167.4-microsoft-standard-WSL2
# ubuntu24  libtool: libtool (GNU libtool) 2.4.7
#           glibc:   ldd (Ubuntu GLIBC 2.39-0ubuntu8.4) 2.39
#-----------------------------------------------------------------
#           server:  Ubuntu 22.04.5 LTS， 5.15.167.4-microsoft-standard-WSL2
# ubuntu22  libtool: libtool (GNU libtool) 2.4.6
#           glibc:   ldd (Ubuntu GLIBC 2.35-0ubuntu3.8) 2.35
#=================================================================

#====================== modify gdb-16.2.patch ====================
# diff -u ../gdb-16.2/a.c ./gdb-16.2/a.c > a.patch
# Manually apply the a.patch to the corresponding files in gdb-16.2.patch
#=================================================================

LAST_VER="v1.3"

show_help() {
	echo ""
	echo "Usage: $0 [32|64|32 64]"
	echo "  32     Compile 32-bit target"
	echo "  64     Compile 64-bit target"
	echo "  32 64  Compile both 32-bit and 64-bit targets"
	echo ""
	echo "Version: ${LAST_VER}"
	echo "  v1.0: 2025.03.25 crash8.0.6+, gdb-16.2, static, strip. support --vmap."
	echo "  v1.1: 2025.06.12 build on ubuntu22, fix for glibc float error."
	echo "  v1.2: 2025.06.20 add tools uncompress. with parameter --umcompress."
	echo "  v1.3: 2025.10.20 update src code from crash master. crash v9.0.0"
	echo ""
	exit 1
}

build_32bit() {
	echo "Building 32-bit target..."
	export LD_LIBRARY_PATH=/lib/i386-linux-gnu:$LD_LIBRARY_PATH
	#LDFLAGS='-static -static-libgcc -s -L/lib/i386-linux-gnu -lmpfr -lgmp -ldl -lm -ltinfo'
	rm ./gdb-16.2 -rf
	make target=ARM clean
	make target=ARM -j$(nproc)
	mkdir -p ./build
	cp ./crash ./build/crash9_a32_${LAST_VER}
}

build_64bit() {
	echo "Building 64-bit target..."
	export LD_LIBRARY_PATH=/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
	#LDFLAGS='-static -static-libgcc -s -L/lib/x86_64-linux-gnu -lmpfr -lgmp -ldl -lm -ltinfo'
	rm ./gdb-16.2 -rf
	make target=ARM64 clean
	make target=ARM64 -j$(nproc)
	mkdir -p ./build
	cp ./crash ./build/crash9_a64_${LAST_VER}
}

if [ $# -eq 0 ]; then
    show_help
fi

for arg in "$@"; do
	case $arg in
		32)
			build_32bit
			;;
		64)
			build_64bit
			;;
		*)
			show_help
			;;
	esac
done

echo ""
echo "==================================== output =========================================="
ls -lh ./build/crash9_a*
echo ""
file ./build/crash9_a*
echo "======================================================================================"
echo ""
