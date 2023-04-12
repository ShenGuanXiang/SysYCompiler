file debug.bin
set arch arm
set endian little
target remote localhost:12345
break main
continue < debug.in > debug.out 
x/2i $pc
layout src
# layout split
layout regs



# add-auto-load-safe-path ...