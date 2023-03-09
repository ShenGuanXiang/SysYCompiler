SRC_PATH ?= src
INC_PATH += include
BUILD_PATH ?= build
TEST_PATH ?= test/level2-[1-5]
OBJ_PATH ?= $(BUILD_PATH)/obj
BINARY ?= $(BUILD_PATH)/compiler
SYSLIB_PATH ?= sysyruntimelibrary

INC = $(addprefix -I, $(INC_PATH))
SRC = $(shell find $(SRC_PATH)  -name "*.cpp")
CFLAGS = -O0 -g -Wall -std=c++11 $(INC)
FLEX ?= $(SRC_PATH)/lexer.l
LEXER ?= $(addsuffix .cpp, $(basename $(FLEX)))
BISON ?= $(SRC_PATH)/parser.y
PARSER ?= $(addsuffix .cpp, $(basename $(BISON)))
SRC += $(LEXER)
SRC += $(PARSER)
OBJ = $(SRC:$(SRC_PATH)/%.cpp=$(OBJ_PATH)/%.o)
	PARSERH ?= $(INC_PATH)/$(addsuffix .h, $(notdir $(basename $(PARSER))))

TESTCASE = $(shell find $(TEST_PATH) -name "*.sy")
TESTCASE_NUM = $(words $(TESTCASE))
LLVM_IR = $(addsuffix _std.ll, $(basename $(TESTCASE)))
GCC_ASM = $(addsuffix _std.s, $(basename $(TESTCASE)))
OUTPUT_LAB4 = $(addsuffix .toks, $(basename $(TESTCASE)))
OUTPUT_LAB5 = $(addsuffix .ast, $(basename $(TESTCASE)))
OUTPUT_LAB6 = $(addsuffix .ll, $(basename $(TESTCASE)))
OUTPUT_LAB7 = $(addsuffix .s, $(basename $(TESTCASE)))
OUTPUT_RES = $(addsuffix .res, $(basename $(TESTCASE)))
OUTPUT_BIN = $(addsuffix .bin, $(basename $(TESTCASE)))
OUTPUT_LOG = $(addsuffix .log, $(basename $(TESTCASE)))

.phony:all app run gdb testlab4 testlab5 testlab6 testlab7 test clean clean-all clean-test clean-app llvmir gccasm

all:app

$(LEXER):$(FLEX)
		@flex -o $@ $<

$(PARSER):$(BISON)
		@bison -o $@ $< --warnings=error=all --defines=$(PARSERH)

$(OBJ_PATH)/%.o:$(SRC_PATH)/%.cpp
		@mkdir -p $(OBJ_PATH)
			@g++ $(CFLAGS) -c -o $@ $<

$(BINARY):$(OBJ)
		@g++ -O0 -g -o $@ $^

app:$(LEXER) $(PARSER) $(BINARY)

#old version
oldll:app
		@$(BINARY) -o debug.ll -i debug.sy -O2
oldasm:app
		@$(BINARY) -o debug.s -S debug.sy -O2
# the following line is for debug new optimization
diffll:app
		@$(BINARY) -o old.ll -i debug.sy -O2
		@$(BINARY) -o new.ll -i debug.sy  -M
diffasm:app
		@$(BINARY) -o old.s -S debug.sy -O2
		@$(BINARY) -o new.s -S debug.sy -M
newll:app
		@$(BINARY) -o debug.ll -i debug.sy -M 
newasm:app
		@$(BINARY) -o debug.s -S debug.sy -M 
runll:app
		clang -o debug.bin debug.ll sysyruntimelibrary/sylib.c
		./debug.bin
		echo "\n"$$?
runasm:app
		arm-linux-gnueabihf-gcc -mcpu=cortex-a72 -o debug.bin debug.s sysyruntimelibrary/libsysy.a
		qemu-arm -L /usr/arm-linux-gnueabihf debug.bin
		echo "\n"$$?
cleandebug:
		rm debug.ll debug.s debug.bin 

ast:app
		@$(BINARY) -o debug.ast -a debug.sy  2> /dev/null

gdb:app
		@gdb $(BINARY)

$(OBJ_PATH)/lexer.o:$(SRC_PATH)/lexer.cpp
		@mkdir -p $(OBJ_PATH)
			@g++ $(CFLAGS) -c -o $@ $<

$(TEST_PATH)/%.toks:$(TEST_PATH)/%.sy
		@$(BINARY) $< -o $@ -t

$(TEST_PATH)/%.ast:$(TEST_PATH)/%.sy
		@$(BINARY) $< -o $@ -a

$(TEST_PATH)/%.ll:$(TEST_PATH)/%.sy
		@$(BINARY) $< -o $@ -i	

$(TEST_PATH)/%_std.ll:$(TEST_PATH)/%.sy
		@clang -x c $< -S -m32 -emit-llvm -o $@ 

$(TEST_PATH)/%_std.s:$(TEST_PATH)/%.sy
		@arm-linux-gnueabihf-gcc -x c $< -S -o $@ 

$(TEST_PATH)/%.s:$(TEST_PATH)/%.sy
		@timeout 5s $(BINARY) $< -o $@ -S 2>$(addsuffix .log, $(basename $@))
			@[ $$? != 0 ] && echo "COMPILE FAIL: $(notdir $<)" || echo "COMPILE SUCCESS: $(notdir $<)"

llvmir:$(LLVM_IR)

gccasm:$(GCC_ASM)

testlab4:app $(OUTPUT_LAB4)

testlab5:app $(OUTPUT_LAB5)

testlab6:app $(OUTPUT_LAB6)

testlab7:app $(OUTPUT_LAB7)
.ONESHELL:
testllvm:app
	@success=0
	@for file in $(sort $(TESTCASE))
	do
		IR=$${file%.*}.ll
		LOG=$${file%.*}.log
		BIN=$${file%.*}.bin
		RES=$${file%.*}.res
		IN=$${file%.*}.in
		OUT=$${file%.*}.out
		FILE=$${file##*/}
		FILE=$${FILE%.*}
		timeout 5s $(BINARY) $${file} -o $${IR} -i -M 2>$${LOG}

		RETURN_VALUE=$$?
		if [ $$RETURN_VALUE = 124 ]; then
			echo "FAIL: $${FILE}\tCompile Timeout"
			continue
		else if [ $$RETURN_VALUE != 0 ]; then
			echo "FAIL: $${FILE}\tCompile Error"
			continue
			fi
		fi
		clang -o $${BIN} $${IR} $(SYSLIB_PATH)/sylib.c >>$${LOG} 2>&1
		if [ $$? != 0 ]; then
			echo "FAIL: $${FILE}\tAssemble Error"
		else
			if [ -f "$${IN}" ]; then
				timeout 2s $${BIN} <$${IN} >$${RES} 2>>$${LOG}
			else
				timeout 2s $${BIN} >$${RES} 2>>$${LOG}
			fi
			RETURN_VALUE=$$?
			FINAL=`tail -c 1 $${RES}`
			[ $${FINAL} ] && echo "\n$${RETURN_VALUE}" >> $${RES} || echo "$${RETURN_VALUE}" >> $${RES}
			if [ "$${RETURN_VALUE}" = "124" ]; then
				echo "FAIL: $${FILE}\tExecute Timeout"
			else if [ "$${RETURN_VALUE}" = "127" ]; then
				echo "FAIL: $${FILE}\tExecute Error"
				else
					diff -Z $${RES} $${OUT} >/dev/null 2>&1
					if [ $$? != 0 ]; then
						echo "FAIL: $${FILE}\tWrong Answer"
					else
						success=$$((success + 1))
						echo "PASS: $${FILE}"
					fi
				fi
			fi
		fi
	done
	echo "Total: $(TESTCASE_NUM)\tAccept: $${success}\tFail: $$(($(TESTCASE_NUM) - $${success}))"
	[ $(TESTCASE_NUM) = $${success} ] && echo "All Accepted. Congratulations!"
	:

testasm:app
	@success=0
	@for file in $(sort $(TESTCASE))
	do
		ASM=$${file%.*}.s
		LOG=$${file%.*}.log
		BIN=$${file%.*}.bin
		RES=$${file%.*}.res
		IN=$${file%.*}.in
		OUT=$${file%.*}.out
		FILE=$${file##*/}
		FILE=$${FILE%.*}
		timeout 5s $(BINARY) $${file} -o $${ASM} -S -M 2>$${LOG}
		RETURN_VALUE=$$?
		if [ $$RETURN_VALUE = 124 ]; then
			echo "FAIL: $${FILE}\tCompile Timeout"
			continue
		else if [ $$RETURN_VALUE != 0 ]; then
			echo "FAIL: $${FILE}\tCompile Error"
			continue
			fi
		fi
		arm-linux-gnueabihf-gcc -mcpu=cortex-a72 -o $${BIN} $${ASM} $(SYSLIB_PATH)/libsysy.a >>$${LOG} 2>&1
		if [ $$? != 0 ]; then
			echo "FAIL: $${FILE}\tAssemble Error"
		else
			if [ -f "$${IN}" ]; then
				timeout 2s qemu-arm -L /usr/arm-linux-gnueabihf $${BIN} <$${IN} >$${RES} 2>>$${LOG}
			else
				timeout 2s qemu-arm -L /usr/arm-linux-gnueabihf $${BIN} >$${RES} 2>>$${LOG}
			fi
			RETURN_VALUE=$$?
			FINAL=`tail -c 1 $${RES}`
			[ $${FINAL} ] && echo "\n$${RETURN_VALUE}" >> $${RES} || echo "$${RETURN_VALUE}" >> $${RES}
			if [ "$${RETURN_VALUE}" = "124" ]; then
				echo "FAIL: $${FILE}\tExecute Timeout"
			else if [ "$${RETURN_VALUE}" = "127" ]; then
				echo "FAIL: $${FILE}\tExecute Error"
				else
					diff -Z $${RES} $${OUT} >/dev/null 2>&1
					if [ $$? != 0 ]; then
						echo "FAIL: $${FILE}\tWrong Answer"
					else
						success=$$((success + 1))
						echo "PASS: $${FILE}"
					fi
				fi
			fi
		fi
	done
	echo "Total: $(TESTCASE_NUM)\tAccept: $${success}\tFail: $$(($(TESTCASE_NUM) - $${success}))"
	[ $(TESTCASE_NUM) = $${success} ] && echo "All Accepted. Congratulations!"
	:



clean-app:
		@rm -rf $(BUILD_PATH) $(PARSER) $(LEXER) $(PARSERH)

clean-test:
		@rm -rf $(OUTPUT_LAB4) $(OUTPUT_LAB5) $(OUTPUT_LAB6) $(OUTPUT_LAB7) $(OUTPUT_LOG) $(OUTPUT_BIN) $(OUTPUT_RES) $(LLVM_IR) $(GCC_ASM) ./example.ast ./example.ll ./example.s

clean-all:clean-test clean-app

clean:clean-all
