REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := rogue_step2
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
