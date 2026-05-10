REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET := save_step4
SRC    := main.c

# 第 10 章は endian/sizeof 系の警告を厳しめに
CFLAGS += -Wconversion

.PHONY: all inspect demo clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

inspect: $(TARGET)
	./$(TARGET) inspect

# 章本文で実演する流れ:
#   1) naive で保存して xxd で padding を見る
#   2) explicit で保存して xxd で詰まっていることを見る
demo: $(TARGET)
	./$(TARGET) save_naive    naive.save
	./$(TARGET) save_explicit explicit.save
	@echo "--- naive.save (padding 注意) ---"
	xxd naive.save
	@echo "--- explicit.save ---"
	xxd explicit.save

clean:
	rm -f $(TARGET) *.o naive.save explicit.save
