#ifndef QEMU_HECA_H_
#define QEMU_HECA_H_

extern int heca_enabled;
extern int heca_is_master;

void notify(const char* msg);
void qemu_heca_init(void);

#endif /* QEMU_HECA_H_ */
