#include <stddef.h>
#include "awk.h"
#include <stdio.h>
int main() { printf("%zu\n", offsetof(Cell, fval)); return 0; }
