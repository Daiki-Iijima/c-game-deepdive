REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := tetris_step1
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run massif valgrind clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# Massif: heap 使用量を時系列で記録 → ms_print で読む
massif: $(TARGET)
	valgrind --tool=massif --massif-out-file=massif.out ./$(TARGET)
	ms_print massif.out | less

valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET)

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM massif.out
