#ifndef SYSEMU_H
#define SYSEMU_H
/* Misc. things related to the system emulator.  */

#include "qapi/error.h"

/* vl.c */

struct uc_struct;


int vm_start(struct uc_struct *);

void qemu_system_reset_request(struct uc_struct*);

#endif
