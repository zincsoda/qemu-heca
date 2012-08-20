#ifndef QEMU_HECA_H_
#define QEMU_HECA_H_

extern int heca_enabled;
extern int heca_is_master;
extern struct svm_data *svm_array;
extern svm_count;
extern struct unmap_data *unmap_array;
extern mr_count;

void qemu_heca_init(void);

#endif /* QEMU_HECA_H_ */
