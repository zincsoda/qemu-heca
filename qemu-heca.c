#include <stdio.h>
#include "qemu-heca.h"

int heca_enabled = 0;
int heca_is_master = 0;

void qemu_heca_init(void) {
    if (heca_is_master)
        printf("initializing heca master\n");
    else
        printf("initializing heca client\n");
}
