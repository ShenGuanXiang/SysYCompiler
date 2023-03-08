# make run
clang -o debug.bin debug.ll sysyruntimelibrary/sylib.c
./debug.bin
echo -e "\n"$?