REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET := buggy
SRC    := buggy.c

.PHONY: all asan run_double_free run_use_after_free run_oob run_uninit clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

asan: CFLAGS  += -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

# 章本文の流れに沿って、 各バグを valgrind / asan で観察する
run_double_free: $(TARGET)
	-valgrind --leak-check=full ./$(TARGET) --bug=double-free

run_use_after_free: $(TARGET)
	-valgrind ./$(TARGET) --bug=use-after-free

run_oob:
	$(MAKE) clean
	$(MAKE) asan
	-./$(TARGET) --bug=oob

run_uninit: $(TARGET)
	-valgrind ./$(TARGET) --bug=uninit

clean:
	rm -f $(TARGET) *.o
