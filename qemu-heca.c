#include <stdio.h>
#include <string.h>
#include "qemu-heca.h"
#include "libheca.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "memory.h"
#include "exec-memory.h"
#include "migration.h"

#define PAGE_SIZE 4096

uint32_t heca_enabled = 0;
uint32_t heca_is_master = 0;

uint32_t dsm_id;

uint32_t svm_count = 0;
uint32_t mr_count = 0;

struct svm_data *svm_array;
struct unmap_data *mr_array;

uint32_t local_svm_id;
struct sockaddr_in master_addr;

static inline int qemu_heca_assign_master_mem(void *addr, uint64_t sz)
{
    int i;
    void *pos = addr;

    for (i = 0; i < mr_count; i++) {
        if (pos > addr + sz)
            return -1;
        mr_array[i].addr = pos;
        pos += mr_array[i].sz;
    }

    return 0;
}

void qemu_heca_init(void *qemu_mem_addr, uint64_t qemu_mem_size) 
{
    int fd;

    if (heca_is_master) {
        DEBUG_PRINT("initializing heca master\n");
        if (qemu_heca_assign_master_mem(qemu_mem_addr, qemu_mem_size) < 0) {
            DEBUG_PRINT("not enough mem allocated in vm for memory regions\n");
            exit(1);
        }
        fd = dsm_master_init(svm_count, svm_array, mr_count, mr_array);
    } else {
        DEBUG_PRINT("initializing heca client\n");
        fd = dsm_client_init(qemu_mem_addr, qemu_mem_size, local_svm_id,
                &master_addr);
    }

    if (fd < 0) {
        DEBUG_PRINT("Error initializing\n");
        exit(1);
    }
    DEBUG_PRINT("Heca is ready..\n");
    //dsm_cleanup(fd); 
}


/* helper functions for parsing commandline */
static void get_param(char *target, const char *name, int size, 
    const char *optarg)
{
    if (get_param_value(target, size, name, optarg) == 0) {
        fprintf(stderr, "[DSM] error in value %s\n", name);
        exit(1);
    }
}

static uint32_t get_param_int(const char *name, const char *optarg)
{
    char target[128];
    uint32_t result = 0;

    get_param(target, name, 128, optarg);
    result = strtoull(target, NULL, 10); 
    if (result <= 0 || (result & 0xFFFF) != result) {
        fprintf(stderr, "[DSM] error in value %s\n", name);
        exit(1);
    }
    return result;
}


/* setup data for qemu_heca_init to setup master and slave nodes */
void qemu_heca_parse_master_commandline(const char* optarg)
{
    GSList* svm_list = NULL;
    GSList* mr_list = NULL;

    char nodeinfo_option[128];

    /* dsm general info */
    dsm_id = get_param_int("dsmid", optarg);
    DEBUG_PRINT("dsm_id = %d\n", dsm_id);
    local_svm_id = 1; // always 1 for master
    DEBUG_PRINT("local_svm_id = %d\n", local_svm_id);

    /* per-svm info: id, ip, port */
    get_param(nodeinfo_option, "vminfo", sizeof(nodeinfo_option), optarg);
    const char *p = nodeinfo_option;
    char h_buf[200];
    char l_buf[200];
    const char *q;
    uint32_t i;
    uint32_t tcp_port;

    while (*p != '\0') {
        struct svm_data *next_svm = g_malloc0(sizeof(struct svm_data));

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = get_opt_name(l_buf, sizeof(l_buf), h_buf, ':');
        q++;

        next_svm->dsm_id = dsm_id;
        next_svm->svm_id = strtoull(l_buf, NULL, 10);
        next_svm->local = 0;
        if ((next_svm->svm_id & 0xFFFF) != next_svm->svm_id) {
            fprintf(stderr, "[HECA] Invalid svm_id: %d\n",
                    (int)next_svm->svm_id);
            exit(1);
        }
        DEBUG_PRINT("svm id is: %d\n", next_svm->svm_id);

        // Parse node IP
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        strcpy(next_svm->ip, l_buf);
        DEBUG_PRINT("ip is: %s\n", next_svm->ip);

        // Parse rdma port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->port = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("port is: %d\n", next_svm->port);

        // Parse tcp port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        tcp_port = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("tcp port is (not passed to libheca): %d\n", tcp_port);

        svm_list = g_slist_append(svm_list, next_svm);
        svm_count++;
    }

    // Now, we setup the svm_array with the svms created above 
    svm_array = calloc(svm_count, sizeof(struct svm_data));
    struct svm_data *svm_ptr;
    for (i = 0; i < svm_count; i++) {
        svm_ptr = g_slist_nth_data(svm_list, i);
        memcpy(&svm_array[i], svm_ptr, sizeof(struct svm_data));
    }
    g_slist_free(svm_list);

    /* mr info: sizes, owners */
    get_param(nodeinfo_option, "mr", sizeof(nodeinfo_option), optarg);
    p = nodeinfo_option;

    while (*p != '\0') {
        struct unmap_data *next_mr = g_malloc0(sizeof(struct unmap_data));

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = h_buf;

        // Set dsm id
        next_mr->dsm_id = dsm_id;

        // get memory region id
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_mr->id = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("mr id: %lld\n", (long long int)next_mr->addr);

        // get memory size
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_mr->sz = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("mr sz: %d\n", (int) next_mr->sz);

        // check for correct memory size
        if (next_mr->sz % TARGET_PAGE_SIZE != 0) {
            fprintf(stderr, "HECA: Wrong mem size. \n \
                It has to be a multiple of %d\n", (int)TARGET_PAGE_SIZE);
            exit(1);
        }

        // get all svms for this memory region
        memset(next_mr->svm_ids, 0, sizeof(next_mr->svm_ids[0]) * MAX_SVM_IDS);
        for (i = 0; *q != '\0'; i++) {
            if (i == MAX_SVM_IDS) {
                fprintf(stderr, "HECA: Too many svms for memory region\n");
                exit(1);
            }

            q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
            if (strlen(q))
                q++;
            next_mr->svm_ids[i] = strtoull(l_buf, NULL, 10);
            DEBUG_PRINT("adding mr owner: %d\n", next_mr->svm_ids[i]);
        }

        // Set array of svms for each unmap region
        mr_list = g_slist_append(mr_list, next_mr);
        mr_count++;
    }

    // Now, we setup the mr_array with the unmap_data structs created above
    mr_array = calloc(mr_count, sizeof(struct unmap_data));
    for (i = 0; i < mr_count; i++) {
        memcpy(&mr_array[i], g_slist_nth_data(mr_list, i),
                sizeof(struct unmap_data));
    }

    g_slist_free(mr_list);
}

void qemu_heca_parse_client_commandline(const char* optarg) 
{
    dsm_id = get_param_int("dsmid", optarg);
    DEBUG_PRINT("dsm_id = %d\n", dsm_id);

    local_svm_id = get_param_int("vmid", optarg);
    DEBUG_PRINT("local_svm_id = %d\n", local_svm_id);

    char masterinfo_option[128];
    get_param(masterinfo_option, "master", sizeof(masterinfo_option), optarg);

    char l_buf[200];
    const char *q = masterinfo_option;
 
    q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
    q++;
    char ip[100];
    strcpy(ip, l_buf);
    DEBUG_PRINT("ip is : %s\n",ip);

    // Parse rdma port
    q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
    q++;
    int port = strtoull(l_buf, NULL, 10);
    DEBUG_PRINT("port is : %d\n",port);

    // Parse tcp port
    q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
    q++;
    int tcp_port = strtoull(l_buf, NULL, 10);
    DEBUG_PRINT("tcp port: %d\n", tcp_port);

    bzero((char*) &master_addr, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(tcp_port);
    master_addr.sin_addr.s_addr = inet_addr(ip);
}

static inline MemoryRegion *qemu_heca_get_system_mr(void)
{
    RAMBlock *block;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr, "pc.ram", strlen(block->idstr)) == 0)
            return block->mr;
    }
    return NULL;
}

void *qemu_heca_get_system_ram_ptr(void)
{
    MemoryRegion *sys_mr = qemu_heca_get_system_mr();
    if (sys_mr)
        return memory_region_get_ram_ptr(sys_mr);
    return NULL;
}

uint64_t qemu_heca_get_system_ram_size(void)
{
    MemoryRegion *sys_mr = qemu_heca_get_system_mr();
    if (sys_mr)
        return memory_region_size(sys_mr);
    return 0;
}

void qemu_heca_touch_all_ram(void)
{
    ram_addr_t addr;
    uint64_t *a;
    uint64_t val = 0;
    uint64_t count =0;

    RAMBlock *block;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr,"pc.ram",strlen(block->idstr)) == 0)
        {
            for (addr = 0; addr < block->length; addr += TARGET_PAGE_SIZE) {
                a = qemu_get_ram_ptr(addr);
                val += *a;
                count++;
            }
        }
    }
    printf("STEVE: accessed %ld pages\n", count);
}

