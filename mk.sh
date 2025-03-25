#!/bin/bash

#============================= env setup =========================
# ubuntu 24.0, compile crash static：
# X86_64bit lib-dev:
# sudo apt install binutils-aarch64-linux-gnu gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
# sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi libc6-arm64-cross
# sudo apt update
# sudo apt install autopoint autoconf automake libtool-bin build-essential bison zlib1g-dev flex
# sudo apt install libgmp-dev libmpfr-dev m4 libtool gettext texinfo gperf groff

# i386_32bit lib-dev:
# sudo dpkg --add-architecture i386
# sudo apt update
# sudo apt install gcc-multilib g++-multilib binutils-multiarch
# sudo apt install libc6-dev-i386 libstdc++-12-dev:i386 libgcc-12-dev:i386 zlib1g-dev:i386
# sudo apt install libgmp-dev:i386 libmpfr-dev:i386 m4:i386 libtool:i386
#=================================================================

#========================== libs version =========================
# server:  Ubuntu 24.04.1 LTS， 5.15.167.4-microsoft-standard-WSL2
# libtool: libtool (GNU libtool) 2.4.7
# glibc:   ldd (Ubuntu GLIBC 2.39-0ubuntu8.4) 2.39
#=================================================================

LAST_VER="v10"

show_help() {
    echo ""
    echo "Usage: $0 [32|64|32 64]"
    echo "  32     Compile 32-bit target"
    echo "  64     Compile 64-bit target"
    echo "  32 64  Compile both 32-bit and 64-bit targets"
    echo ""
    echo "Version: ${LAST_VER}"
    echo "  v10: 2025.03.25 crash8.0.6+, gdb-16.2, static, strip. support --vmap."
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
    cp ./crash ./crash8_a32_${LAST_VER}
}

build_64bit() {
    echo "Building 64-bit target..."
    export LD_LIBRARY_PATH=/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
    #LDFLAGS='-static -static-libgcc -s -L/lib/x86_64-linux-gnu -lmpfr -lgmp -ldl -lm -ltinfo'
    rm ./gdb-16.2 -rf
    make target=ARM64 clean
    make target=ARM64 -j$(nproc)
    cp ./crash ./crash8_a64_${LAST_VER}
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
ls -lh ./crash8_a*
echo ""
file ./crash8_a*
echo "======================================================================================"
echo ""

