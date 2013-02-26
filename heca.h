#ifndef QEMU_HECA_H_
#define QEMU_HECA_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <wordexp.h>

#define DEBUG 1

#include <libheca.h>

#include "memory.h"
#include "qemu-timer.h"

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
    struct sockaddr_in master_addr;
} Heca;

extern Heca heca;

void qemu_heca_init(void* ram_ptr, uint64_t ram_size);
void qemu_heca_master_cmdline_init(const char* optarg);
void qemu_heca_client_cmdline_init(const char* optarg);

void qemu_heca_migrate_dest_init(const char* dest_ip, const char* source_ip); 
void qemu_heca_migrate_src_init(const char* dest_ip, int precopy_time);

void *qemu_heca_get_system_ram_ptr(void);
uint64_t qemu_heca_get_system_ram_size(void);

void qemu_heca_touch_all_ram(void);

void qemu_heca_start_mig_timer(uint64_t timeout);
bool qemu_heca_is_mig_timer_expired(void);
void qemu_heca_set_post_copy_phase(void);
bool qemu_heca_is_pre_copy_phase(void);

int qemu_heca_unmap_memory(void *addr, size_t size);
int qemu_heca_unmap_dirty_bitmap(uint8_t *bitmap, uint32_t bitmap_size);

extern int ram_send_block_info(QEMUFile *f);
extern int get_ram_unmap_info(QEMUFile *f);

void parse_heca_master_commandline(const char* optarg);
void parse_heca_client_commandline(const char* optarg);

#endif /* QEMU_HECA_H_ */

