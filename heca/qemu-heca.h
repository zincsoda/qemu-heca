#ifndef QEMU_HECA_H_
#define QEMU_HECA_H_

#include <stdint.h>
#include "monitor.h"
#include "qemu-timer.h"

#define DEBUG 1
#define DEBUG_PRINT(fmt, args...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, \
                __FILE__, __LINE__, __FUNCTION__, ##args); } while (0)

typedef struct Heca {
    bool is_enabled;
    bool is_master;
    uint8_t dsm_id;
    int rdma_fd;
    int rdma_port;
    int tcp_sync_port;
    uint32_t svm_count;
    uint32_t mr_count;
    struct svm_data *svm_array;
    struct unmap_data *mr_array;
    uint32_t local_svm_id;
    QEMUTimer *migration_timer;
    bool is_timer_expired;
    bool is_iterative_phase;
} Heca;

extern Heca heca;

void qemu_heca_init(void *qemu_mem_addr, uint64_t qemu_mem_size);
void qemu_heca_migrate_dest_init(const char* dest_ip, const char* source_ip); 
void qemu_heca_migrate_src_init(const char* dest_ip, int precopy_time);
void *qemu_heca_get_system_ram_ptr(void);
uint64_t qemu_heca_get_system_ram_size(void);
int qemu_heca_unmap_memory(void *addr, size_t size);
void qemu_heca_touch_all_ram(void);
void qemu_heca_start_mig_timer(uint64_t timeout);
bool qemu_heca_is_mig_timer_expired(void);
void qemu_heca_set_post_copy_phase(void);
bool qemu_heca_is_pre_copy_phase(void);
int qemu_heca_unmap_dirty_bitmap(uint8_t *bitmap, uint32_t bitmap_size);
int ram_send_block_info(QEMUFile *f);
int get_ram_unmap_info(QEMUFile *f);

#endif /* QEMU_HECA_H_ */
