# make run
arm-linux-gnueabihf-gcc -mcpu=cortex-a72 -march=armv7 -fPIC -o debug.bin debug.s sysyruntimelibrary/libsysy.a
qemu-arm -s 1000000000000000 -L /usr/arm-linux-gnueabihf debug.bin <debug.in >debug.out
# qemu-arm -L /usr/arm-linux-gnueabihf debug.bin
echo -e "\n"$?