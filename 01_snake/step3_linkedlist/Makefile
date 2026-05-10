REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := snake_step3
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

.PHONY: all run bug valgrind asan clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

asan: CFLAGS  += -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

run: $(TARGET)
	./$(TARGET)

# 二重 free を仕込んだモードで起動。プレイ後にコアダンプ or 異常終了する。
bug: $(TARGET)
	./$(TARGET) --bug=double-free

# valgrind は対話プレイには不向きなので、--bug=double-free と組み合わせて
# 短時間で問題を見せるのが定石。実際は短いスクリプト入力を流す方が好ましい。
valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) --bug=double-free

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
