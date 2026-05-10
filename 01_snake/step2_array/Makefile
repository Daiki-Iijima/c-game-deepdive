REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET  := snake_step2
SRC     := main.c
LIB_SRC := $(LIB_TTY_SRC)

# 章 2 から本格的に sizeof / offsetof を扱うため -Wconversion を投入する。
# uint8_t / int / size_t の暗黙変換警告が学習材料になる。
CFLAGS += -Wconversion

.PHONY: all run inspect asan clean

all: $(TARGET)

$(TARGET): $(SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB_SRC) $(LDFLAGS) $(LDLIBS)

asan: CFLAGS  += -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

run: $(TARGET)
	./$(TARGET)

inspect: $(TARGET)
	./$(TARGET) --inspect

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
