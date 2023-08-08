# make run
clang -o debug.bin -g debug.ll sysyruntimelibrary/sylib.c
./debug.bin <debug.in >debug.out
echo -e "\n"$?