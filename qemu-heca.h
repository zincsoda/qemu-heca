#ifndef QEMU_HECA_H_
#define QEMU_HECA_H_

#include <stdint.h>
#include "qemu-option.h"
#include "monitor.h"

#define DEBUG 1

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

#define DEBUG_PRINT(fmt, args...) \
        do { if (DEBUG_TEST) fprintf(stderr, "%s:%d:%s(): " fmt, \
                __FILE__, __LINE__, __FUNCTION__, ##args); } while (0)

extern uint32_t heca_enabled;
extern uint32_t heca_is_master;

extern uint32_t dsm_id;

extern uint32_t svm_count;
extern uint32_t mr_count;

extern struct svm_data *svm_array;
extern struct unmap_data *unmap_array;

extern uint32_t local_svm_id;
extern struct sockaddr_in master_addr;

void qemu_heca_init(void *qemu_mem_addr, uint64_t qemu_mem_size);
void qemu_heca_parse_master_commandline(const char* optarg);
void qemu_heca_parse_client_commandline(const char* optarg);
void *qemu_heca_get_system_ram_ptr(void);
uint64_t qemu_heca_get_system_ram_size(void);

void qemu_heca_touch_all_ram(void);


#endif /* QEMU_HECA_H_ */
