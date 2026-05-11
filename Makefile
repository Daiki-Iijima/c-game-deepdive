# Workspace 一括ビルド。各章ディレクトリは独自 Makefile を持つ。
# 共通フラグはここで定義し、各章 Makefile から `include ../../Makefile.common` で参照する。
# (= ルート Makefile と Makefile.common の二段構成)

CHAPTERS := \
	01_snake/step1_termios/s1_scanf \
	01_snake/step1_termios/s2_canon \
	01_snake/step1_termios/s3_echo \
	01_snake/step1_termios/s4_isig \
	01_snake/step1_termios/s5_full \
	01_snake/step2_array \
	01_snake/step3_linkedlist \
	02_tetris/step1_heap \
	02_tetris/step2_bitwise \
	02_tetris/step3_collision \
	03_roguelike/step1_map \
	03_roguelike/step2_signal \
	03_roguelike/step3_ipc \
	03_roguelike/step4_save \
	04_tools/bug_hunting \
	04_tools/binary_anatomy

.PHONY: all clean list $(CHAPTERS)

all: $(CHAPTERS)

$(CHAPTERS):
	@if [ -f $@/Makefile ]; then \
		$(MAKE) -C $@; \
	else \
		echo "(skip) $@ has no Makefile yet"; \
	fi

clean:
	@for d in $(CHAPTERS); do \
		if [ -f $$d/Makefile ]; then $(MAKE) -C $$d clean; fi; \
	done

list:
	@printf '%s\n' $(CHAPTERS)
