REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := rogue_step3
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run strace clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# fork/pipe/dup2 を strace で見る (子も追跡)
strace: $(TARGET)
	strace -f -e trace=clone,fork,pipe,pipe2,dup2,read,write,close ./$(TARGET) 2>&1 | head -200

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
