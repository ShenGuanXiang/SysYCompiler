# make run
clang -o debug.bin -g debug.ll sysyruntimelibrary/sylib.c
./debug.bin
echo -e "\n"$?