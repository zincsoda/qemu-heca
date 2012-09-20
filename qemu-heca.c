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
struct unmap_data *unmap_array;

uint32_t local_svm_id;
struct sockaddr_in master_addr;

void qemu_heca_init(unsigned long qemu_mem_addr) 
{
    if (heca_is_master) {
        
        DEBUG_PRINT("initializing heca master\n");
        
        int fd;
        fd = dsm_master_init((void*)qemu_mem_addr, 0, svm_count, svm_array, mr_count, unmap_array);
        if (fd < 0) {
            DEBUG_PRINT("Error initializing master node\n");
            exit(1);
        }
        
        DEBUG_PRINT("Heca is ready..\n");

        //dsm_cleanup(fd);
        //free(svm_array);
        //free(unmap_array);
        
    } else {
        DEBUG_PRINT("initializing heca client\n");

        int fd;
        
        fd = dsm_client_init ((void *)qemu_mem_addr, 0, local_svm_id, &master_addr);
        if (fd < 0 ) {
            DEBUG_PRINT("Error initializing client node\n");
            exit(1);
        }

        DEBUG_PRINT("Heca is ready..\n");

        //dsm_cleanup(fd); 
 
    }
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


void qemu_heca_parse_master_commandline(const char* optarg) {
    /*
     * setup data for qemu_heca_init to setup master and slave nodes 
     */
    GSList* svm_list = NULL;
    GSList* unmap_list = NULL;

    char nodeinfo_option[128];

    dsm_id = get_param_int("dsmid", optarg);
    DEBUG_PRINT("dms_id = %d\n", dsm_id);

    local_svm_id = 1; // always 1 for master
    DEBUG_PRINT("local_svm_id = %d\n", local_svm_id);

    uint32_t tcp_port;

    get_param(nodeinfo_option, "vminfo", sizeof(nodeinfo_option), optarg);
    const char *p = nodeinfo_option;
    char h_buf[200];
    char l_buf[200];
    const char *q;
    uint32_t i;

    // This loop gets the vminfo details for each svm
    while (*p != '\0') {
        struct svm_data *next_svm = g_malloc0(sizeof(struct svm_data));
        
        // Set dsm_id
        next_svm->dsm_id = dsm_id;

        // Set offset
        next_svm->offset = 0;

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = h_buf;

        // Parse vm id
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->svm_id = strtoull(l_buf, NULL, 10);
        if ((next_svm->svm_id & 0xFFFF ) != next_svm->svm_id)
        {
            fprintf(stderr, "[HECA] Invalid svm_id: %d\n",
                    (int)next_svm->svm_id);
            exit(1);
        }
        DEBUG_PRINT("svm id is : %d\n",next_svm->svm_id);

        // Parse node IP
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        strcpy(next_svm->ip, l_buf);
        DEBUG_PRINT("ip is : %s\n",next_svm->ip);

        // Parse rdma port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->port = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("port is : %d\n",next_svm->port);

        // Parse tcp port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        tcp_port = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("tcp port is (not passed to libheca): %d\n",tcp_port);

        svm_list = g_slist_append(svm_list, next_svm);
        svm_count++;
    }

    // Now, we setup the svm_array with the svms created above 
    svm_array = calloc(svm_count, sizeof(struct svm_data));
    struct svm_data *svm_ptr;
    for (i = 0; i < svm_count; i++)
    {
        svm_ptr = g_slist_nth_data(svm_list, i);
        memcpy(&svm_array[i], svm_ptr, sizeof(struct svm_data));
    }
    g_slist_free(svm_list);


    get_param(nodeinfo_option, "mr", sizeof(nodeinfo_option), optarg);
    p = nodeinfo_option;

    while (*p != '\0') {
        struct unmap_data *next_unmap = g_malloc0(sizeof(struct unmap_data));

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = h_buf;

        // Set dsm id
        next_unmap->dsm_id = dsm_id;

        // get memory start offset
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_unmap->addr = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("unmap addr: %lld\n", (long long int)next_unmap->addr);

        // get memory size
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_unmap->sz = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("unmap sz: %d\n", (int) next_unmap->sz);

        // check for correct memory size
        if (next_unmap->sz % TARGET_PAGE_SIZE != 0) {
            fprintf(stderr, "HECA: Wrong mem size. \n \
                It has to be a multiple of %d\n", (int)TARGET_PAGE_SIZE);
            exit(1);
        }

        // get all svms for this memory region
        for (i = 0; i < MAX_SVM_IDS; i++)
            next_unmap->svm_ids[i] = 0;

        int mr_svm_count = 0;
        while (*q != '\0') {
            q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
            if (strlen(q))
                q++;
            next_unmap->svm_ids[mr_svm_count] = strtoull(l_buf, NULL, 10);
            DEBUG_PRINT("adding svm: %d\n", next_unmap->svm_ids[mr_svm_count]);
            
            mr_svm_count++; 
        }

        // set unmap flag
        next_unmap->unmap = UNMAP_REMOTE;

        // Set array of svms for each unmap region
        unmap_list = g_slist_append(unmap_list, next_unmap);
        mr_count++;
    }

    // Now, we setup the unmap_array with the unmap_data structs created above
    unmap_array = calloc(mr_count, sizeof(struct unmap_data));
    struct unmap_data *unmap_ptr;
    for (i = 0; i < mr_count; i++)
    {
        unmap_ptr = g_slist_nth_data(unmap_list, i);
        memcpy(&unmap_array[i], unmap_ptr, sizeof(struct unmap_data));
    }

    g_slist_free(unmap_list);
}


void qemu_heca_parse_client_commandline(const char* optarg) {

    dsm_id = get_param_int("dsmid", optarg);
    DEBUG_PRINT("dms_id = %d\n", dsm_id);

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


void* qemu_heca_get_system_ram_ptr(void)
{
    //MemoryRegion *sysmem;
    //MemoryRegion *subregion;
    MemoryRegion *mr;
    void * ram_ptr = NULL;

    RAMBlock *block;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr,"pc.ram",strlen(block->idstr)) == 0)
        {
            mr = block->mr;
            ram_ptr = memory_region_get_ram_ptr(mr);
            return ram_ptr;
        }
    }

    /*sysmem = get_system_memory();
    
    QTAILQ_FOREACH(subregion, &sysmem->subregions, subregions_link) {
        printf("STEVE: subregion: %s\n", memory_region_name(subregion));
        if ( strcmp(memory_region_name(subregion), "ram-below-4g" ) == 0 ) {
            mr = subregion;
            ram_ptr = memory_region_get_ram_ptr(mr);
        }
    }
    */

    return ram_ptr;
}

void qemu_heca_touch_all_ram(void)
{
    ram_addr_t addr;
    uint64_t *a;
    uint64_t val = 0;

    RAMBlock *block;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr,"pc.ram",strlen(block->idstr)) == 0)
        {
            for (addr = 0; addr < block->length; addr += TARGET_PAGE_SIZE) {
                a = qemu_get_ram_ptr(addr);
                val += *a;
            }
        }
    }
}

