REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := tetris_step2
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run dump asm clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

# 16-bit パターンを ASCII art で確認
dump: $(TARGET)
	./$(TARGET) --dump-shapes

# 第 12 章で再訪する asm 出力 (今は眺めるだけ)
asm:
	$(CC) $(CFLAGS) -O2 -S main.c -o main.s
	@echo "see main.s"

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM main.s
