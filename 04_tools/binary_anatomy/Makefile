REPO_ROOT := ../../
include $(REPO_ROOT)/Makefile.common

TARGET := sample
SRC    := sample.c

.PHONY: all O2 readelf disas perf clean

all: $(TARGET)

# デバッグ用 (O0)
$(TARGET): $(SRC)
	$(CC) $(CSTD) $(WARNINGS) -O0 -g3 -o $@ $(SRC)

# 第 12 章で見る最適化版
O2: $(SRC)
	$(CC) $(CSTD) $(WARNINGS) -O2 -g -o sample_O2 $(SRC)

# ELF ヘッダ + section 一覧
readelf: $(TARGET)
	@echo "=== ELF header ==="
	readelf -h $(TARGET)
	@echo
	@echo "=== Section headers ==="
	readelf -S $(TARGET)
	@echo
	@echo "=== Dynamic symbols ==="
	readelf --dyn-syms $(TARGET) | head -20

# main / dispatch / hot_loop を逆アセンブル
disas: O2
	@echo "=== dispatch ==="
	objdump -d --disassemble=dispatch sample_O2
	@echo
	@echo "=== hot_loop ==="
	objdump -d --disassemble=hot_loop sample_O2

# perf stat で IPC / cache miss を測る (Linux 実機 or Codespaces 推奨)
perf: O2
	perf stat -e instructions,cycles,L1-dcache-load-misses ./sample_O2 1000000

clean:
	rm -f $(TARGET) sample_O2 *.o *.s
