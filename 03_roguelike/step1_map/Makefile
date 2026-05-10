REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := rogue_step1
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run big clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

big: $(TARGET)
	./$(TARGET) 60 200

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
