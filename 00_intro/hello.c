/* 第 0 章で扱うコンパイルパイプラインの題材。
   gcc -E / -S / -c / (link) と段階的に変換していく。 */
#include <stdio.h>

int main(void) {
    puts("hello, deepdive");
    return 0;
}
