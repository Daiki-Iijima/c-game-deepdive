REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := tetris_step3
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run asan asan_bug valgrind_bug clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

asan: CFLAGS  += -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

run: $(TARGET)
	./$(TARGET)

asan_bug: asan
	./$(TARGET) --bug=oob

valgrind_bug: $(TARGET)
	valgrind --error-exitcode=1 ./$(TARGET) --bug=oob

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
