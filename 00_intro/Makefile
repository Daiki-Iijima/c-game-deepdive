REPO_ROOT := ../
include $(REPO_ROOT)/Makefile.common

TARGET := hello
SRC    := hello.c

.PHONY: all run pipeline clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# 第 0 章本文で実演するコンパイルパイプライン (preprocess → compile → assemble → link)
pipeline: $(SRC)
	$(CC) -E $(SRC) -o hello.i
	$(CC) -S $(SRC) -o hello.s
	$(CC) -c $(SRC) -o hello.o
	$(CC) hello.o -o hello
	@echo "--- file ---"
	@file hello

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) hello.i hello.s hello.o
