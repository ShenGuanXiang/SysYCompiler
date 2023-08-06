arm-linux-gnueabihf-gcc -mcpu=cortex-a72 -march=armv7 -g -o debug.bin debug.s sysyruntimelibrary/libsysy.a -static
file debug.bin
qemu-arm -g 12345 ./debug.bin <debug.in  &
gdb-multiarch ./debug.bin 
# https://www.cxyzjd.com/article/qq_33892117/89500363