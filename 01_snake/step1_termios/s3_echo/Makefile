REPO_ROOT := ../../../
include $(REPO_ROOT)/Makefile.common

TARGET := snake_step1_s3
SRC    := main.c

.PHONY: all run clean asan

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

asan: CFLAGS += -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) *.o $(TARGET).dSYM
