#include "qemu-heca.h"

Heca heca;

static void print_data_structures(void);
static const char* ip_from_uri(const char* uri);
static void heca_config(void);

void qemu_heca_migrate_dest_init(const char* dest_ip, const char* source_ip)
{
    heca_config();
    heca.is_enabled = true;
    heca.dsm_id = 1;          // only need 1 for live migration (LM)
    heca.local_svm_id = 1;    // master node is 1
    heca.svm_count = 2;       // only master and client required for LM
    heca.mr_count = 1;        // only need 1 memory region for LM

    struct svm_data dst_svm = { .dsm_id = 1, .svm_id = 1, .port = heca.rdma_port };
    struct svm_data src_svm = { .dsm_id = 1, .svm_id = 2, .port = heca.rdma_port };
    strncpy(dst_svm.ip, dest_ip, MAX_ADDR_STR);
    strncpy(src_svm.ip, source_ip, MAX_ADDR_STR);

    heca.svm_array = calloc(heca.svm_count, sizeof(struct svm_data));
    heca.svm_array[0] = dst_svm;
    heca.svm_array[1] = src_svm;

    heca.mr_array = calloc(heca.mr_count, sizeof(struct unmap_data));
    struct unmap_data mr = { .dsm_id = 1, .id = 1, .svm_ids = { 2, 0 },
        .unmap = 0, .copy_on_access = 1};
    heca.mr_array[0] = mr;

    void *ram_ptr = qemu_heca_get_system_ram_ptr();
    if (!ram_ptr) {
        DEBUG_PRINT("Error getting ram_ptr to system memory\n");
        exit(1);
    }
    uint64_t ram_sz = qemu_heca_get_system_ram_size();

    heca.mr_array[0].addr = ram_ptr; // only one memory region required for LM
    heca.mr_array[0].sz = ram_sz;

    DEBUG_PRINT("initializing heca master\n");

    //print_data_structures();
    heca.rdma_fd = dsm_master_init(heca.svm_count, 
            heca.svm_array, heca.mr_count, heca.mr_array);

    if (heca.rdma_fd < 0) {
        DEBUG_PRINT("Error initializing master node\n");
        exit(1);
    }

    DEBUG_PRINT("Heca master node is ready..\n");
    //dsm_cleanup(fd); 
}

void qemu_heca_migrate_src_init(const char* uri, int precopy_time)
{
    heca_config();

    heca.is_enabled = true;
    heca.dsm_id = 1;         // only need 1 for live migration (LM)
    heca.local_svm_id = 2;   // client node
    heca.svm_count = 2;      // only master and client required for LM

    const char* dest_ip = ip_from_uri(uri);
    bzero((char*) &heca.master_addr, sizeof(heca.master_addr));
    heca.master_addr.sin_family = AF_INET;
    heca.master_addr.sin_port = htons(heca.tcp_sync_port);
    heca.master_addr.sin_addr.s_addr = inet_addr(dest_ip);

    qemu_heca_start_mig_timer(precopy_time);

    void *ram_ptr = qemu_heca_get_system_ram_ptr();
    uint64_t ram_size = qemu_heca_get_system_ram_size();
    if (!ram_ptr) {
        DEBUG_PRINT("Error getting ram pointer\n");
        exit(1);
    }

    DEBUG_PRINT("initializing heca client node ...\n");
    
    heca.rdma_fd = dsm_client_init(ram_ptr, ram_size, 
            heca.local_svm_id, &heca.master_addr);

    if (heca.rdma_fd < 0 ) {
        DEBUG_PRINT("Error initializing client node\n");
        exit(1);
    }

    DEBUG_PRINT("Heca client node is ready..\n");
    //dsm_cleanup(fd); 
}

static const char* ip_from_uri(const char* uri) 
{
    char char_array_uri[MAX_ADDR_STR];

    strncpy(char_array_uri, uri, MAX_ADDR_STR);

    char* ip = strtok(char_array_uri, ":"); // ip points to uri protocol, e.g. 'tcp'
    ip = strtok(NULL, ":");                 // ip now points to ip address

    return (const char*) ip;
}

static void heca_config(void)
{
    // read file .heca_config. Ideally, all configuration would be contained here.
    wordexp_t result;
    wordexp("~/.heca_config", &result, 0);
    const char* heca_conf_path = result.we_wordv[0];

    FILE *conf_file = fopen(heca_conf_path, "r");
    if (conf_file == NULL) {
        // use default port values
        heca.rdma_port = 4444;
        heca.tcp_sync_port = 4445;
    }

    if (conf_file && fscanf(conf_file, "RDMA_PORT=%d", &heca.rdma_port) < 1) {
        DEBUG_ERROR("Couldn't read RDMA_PORT, using default value of 4444\n");
        heca.rdma_port = 4444;
    };

    if (conf_file && fscanf(conf_file, "TCP_SYNC_PORT=%d", &heca.tcp_sync_port) < 1) {
        DEBUG_ERROR("Couldn't read TCP_SYNC_PORT, using default value of 4445\n");
        heca.tcp_sync_port = 4445;
    };
}

static inline int qemu_heca_assign_master_mem(void *ram_ptr, uint64_t ram_size)
{
    int i;
    void *pos = ram_ptr;

    for (i = 0; i < heca.mr_count; i++) {
        if (pos > ram_ptr + ram_size)
            return -1;
        heca.mr_array[i].addr = pos;
        pos += heca.mr_array[i].sz;
    }

    return 0;
}

static void print_data_structures(void)
{
    int i;
    int j;
    printf("svm_array:\n");
    for (i = 0; i < heca.svm_count; i++) {
        printf("{ .dsm_id = %d, .svm_id = %d, .ip = %s, .port = %d}\n", 
            heca.svm_array[i].dsm_id, heca.svm_array[i].svm_id, 
            heca.svm_array[i].ip, heca.svm_array[i].port);
    }
    printf("mr_array:\n");
    for (i = 0; i < heca.mr_count; i++) {
        printf("{ .dsm_id = %d, .id = %d, .addr = %ld, .sz = %lld, "
               ".unmap = %d, .copy_on_access = %d, "
               ".svm_ids = { ",
                heca.mr_array[i].dsm_id, heca.mr_array[i].id, 
                (unsigned long) heca.mr_array[i].addr, 
                (long long) heca.mr_array[i].sz, heca.mr_array[i].unmap,
                heca.mr_array[i].copy_on_access);
                j = 0;
                while(heca.mr_array[i].svm_ids[j] != 0) {
                    printf("%d, ", heca.mr_array[i].svm_ids[j]);
                    j++;
                }
        printf("0 } }\n");
    }
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


static void * touch_all_ram_worker(void *arg)
{
    target_phys_addr_t block_addr, block_end, addr;
    unsigned long long block_length;

    RAMBlock *block;
    unsigned long buf;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr,"pc.ram",strlen(block->idstr)) == 0)
        {
            block_addr = block->mr->addr;
            block_length = block->length;
            block_end = block_addr + block_length; 
            addr = block_addr;
            while(addr < block_end) {
                addr += TARGET_PAGE_SIZE;
                cpu_physical_memory_read(addr, &buf, sizeof(buf));
            }
        }
    }
    DEBUG_PRINT("Finished reading all ram, now you can terminate the source node.\n");
    /* TODO: Send a message to the source to self terminate */

    pthread_exit(NULL);
}


void qemu_heca_touch_all_ram(void)
{
    pthread_t t;
    pthread_create(&t, NULL, touch_all_ram_worker, NULL);
}


int qemu_heca_unmap_memory(void* addr, size_t size)
{
    int ret = 0;

    // create unmap object for dirty range and unmap it

    struct unmap_data unmap_region;
    unmap_region.addr = addr;
    unmap_region.id = 0; 
    unmap_region.sz = size;
    unmap_region.dsm_id = 1;        // Always 1 in LM
    unmap_region.svm_ids[0] = 2;    // Always set to 2 in LM
    unmap_region.svm_ids[1] = 0;
    unmap_region.unmap = TRUE;

    ret = ioctl(heca.rdma_fd, DSM_UNMAP_RANGE, &unmap_region);
    if (ret)
        return -1;
    else
        return ret;
}

static void mig_timer_expired(void *opaque)
{
    heca.is_timer_expired = true;
    qemu_del_timer(heca.migration_timer);
}

void qemu_heca_start_mig_timer(uint64_t timeout) 
{
    // Start timer with timeout value and mig_timer_expired callback
    heca.migration_timer = qemu_new_timer_ms(rt_clock, mig_timer_expired, NULL);
    qemu_mod_timer(heca.migration_timer, qemu_get_clock_ms(rt_clock) + timeout);
}

bool qemu_heca_is_mig_timer_expired(void)
{
    return heca.is_timer_expired;
}

void qemu_heca_set_post_copy_phase(void)
{
    heca.is_iterative_phase = false;
}

bool qemu_heca_is_pre_copy_phase(void)
{
    return heca.is_iterative_phase;
}

int qemu_heca_unmap_dirty_bitmap(uint8_t *bitmap, uint32_t bitmap_size)
{
    unsigned long host_ram;
    int i, ret = 0;
    void * unmap_addr = NULL;

    host_ram = (unsigned long) qemu_heca_get_system_ram_ptr();
 
    size_t unmap_size = 0;
    unsigned long unmap_offset = -1; // -1 is reset value
    int count = 0;

    for (i = 0; i < bitmap_size; i++) {
        if (bitmap[i] & 0x08) { 
            // page is dirty, flag start of dirty range
            count ++;

            if (unmap_offset == -1) 
                unmap_offset = i * TARGET_PAGE_SIZE;
            unmap_size += TARGET_PAGE_SIZE;

        } else if (unmap_size > 0) {
            // end of dirty range

            unmap_addr = (void*) (host_ram + unmap_offset);
            ret = qemu_heca_unmap_memory(unmap_addr, unmap_size);
            if (ret < 0) {
                return ret;
            }

            // reset 
            unmap_offset = -1;
            unmap_size = 0;
        }
    }
    if (unmap_size > 0) {
        // Last page was dirty but we have finished iterating over bitmap
        unmap_addr = (void*) (host_ram + unmap_offset);
        ret = qemu_heca_unmap_memory(unmap_addr, unmap_size);
        if (ret < 0) {
            return ret;
        }
    }
    qemu_heca_touch_all_ram();

    return ret;
}

void qemu_heca_master_cmdline_init(const char* optarg)
{
    heca.is_enabled = true;
    heca.is_master = true;
    parse_heca_master_commandline(optarg);

    int i;
    for (i = 0; i < heca.mr_count; i++)
    {
        heca.mr_array[i].unmap = TRUE;
        heca.mr_array[i].copy_on_access = FALSE;
    }

}

void qemu_heca_client_cmdline_init(const char* optarg)
{
    heca.is_enabled = true;
    heca.is_master = false;
    parse_heca_client_commandline(optarg);
}

void qemu_heca_init(void* ram_ptr, uint64_t ram_size)
{
    if (heca.is_master) {
        bool debug = true;
        if (debug) 
            print_data_structures();

        qemu_heca_assign_master_mem(ram_ptr, ram_size);

        // init heca
        heca.rdma_fd = dsm_master_init(heca.svm_count, 
                heca.svm_array, heca.mr_count, heca.mr_array);
    } else {
        heca.rdma_fd = dsm_client_init(ram_ptr, ram_size, 
                heca.local_svm_id, &heca.master_addr);
    }

    if (heca.rdma_fd < 0) {
        DEBUG_PRINT("Error initializing master node\n");
        exit(1);
    }

    DEBUG_PRINT("Heca is ready...\n");

}


