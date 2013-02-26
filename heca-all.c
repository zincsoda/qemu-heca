#include "qemu-option.h"
#include "heca.h"
#include <libheca.h>

void *heca_get_system_ram_ptr(void);
uint64_t heca_get_system_ram_size(void);
void heca_touch_all_ram(void);
void heca_start_mig_timer(uint64_t timeout);
int heca_unmap_memory(void *addr, size_t size);
void parse_heca_master_commandline(const char* optarg);
void parse_heca_client_commandline(const char* optarg);

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

Heca heca;

bool heca_is_master(void)
{
    return heca.is_master;
}

bool heca_is_enabled(void)
{
    return heca.is_enabled;
}

static void print_data_structures(void);
static const char* ip_from_uri(const char* uri);
static void heca_config(void);

/* static helper functions for parsing commandline */
static void get_param(char *target, const char *name, int size, 
    const char *optarg)
{
    if (get_param_value(target, size, name, optarg) == 0) {
        DEBUG_ERROR("Could not get parameter value");
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
        printf("error?\n");
        DEBUG_ERROR("Could not get parameter value");
        exit(1);
    }
    return result;
}

/* setup data for heca_init to setup master and slave nodes */
void parse_heca_master_commandline(const char* optarg)
{
    GSList* svm_list = NULL;
    GSList* mr_list = NULL;

    char nodeinfo_option[128];

    /* dsm general info */
    heca.dsm_id = get_param_int("dsmid", optarg);
    DEBUG_PRINT("dsm_id = %d\n", heca.dsm_id);
    heca.local_svm_id = 1; // always 1 for master
    DEBUG_PRINT("local_svm_id = %d\n", heca.local_svm_id);

    /* per-svm info: id, ip, port */
    get_param(nodeinfo_option, "vminfo", sizeof(nodeinfo_option), optarg);
    const char *p = nodeinfo_option;
    char h_buf[500];
    char l_buf[500];
    const char *q;
    uint32_t i;
    uint32_t tcp_port;

    while (*p != '\0') {
        struct svm_data *next_svm = g_malloc0(sizeof(struct svm_data));
        
        next_svm->dsm_id = heca.dsm_id;

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = get_opt_name(l_buf, sizeof(l_buf), h_buf, ':');
        q++;

        next_svm->svm_id = strtoull(l_buf, NULL, 10);
        if ((next_svm->svm_id & 0xFFFF) != next_svm->svm_id) {
            fprintf(stderr, "[HECA] Invalid svm_id: %d\n",
                    (int)next_svm->svm_id);
            exit(1);
        }
        DEBUG_PRINT("svm id is: %d\n", next_svm->svm_id);

        // Parse node IP
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->server.sin_addr.s_addr = inet_addr(l_buf);
        DEBUG_PRINT("ip is: %s\n", l_buf);

        // Parse rdma port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->server.sin_port = htons(strtoull(l_buf, NULL, 10));
        DEBUG_PRINT("port is: %s\n", l_buf);

        // Parse tcp port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        tcp_port = strtoull(l_buf, NULL, 10);
        if (tcp_port) /* FIXME: remove tcp_port - not needed */
            DEBUG_PRINT("tcp port is (not passed to libheca): %d\n", tcp_port);

        svm_list = g_slist_append(svm_list, next_svm);
        heca.svm_count++;
    }

    // Now, we setup the svm_array with the svms created above 
    heca.svm_array = calloc(heca.svm_count, sizeof(struct svm_data));
    struct svm_data *svm_ptr;
    for (i = 0; i < heca.svm_count; i++) {
        svm_ptr = g_slist_nth_data(svm_list, i);
        memcpy(&heca.svm_array[i], svm_ptr, sizeof(struct svm_data));
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
        next_mr->dsm_id = heca.dsm_id;

        // TODO: code to set id
        //next_mr->id = 1;

        // get memory region id
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_mr->mr_id = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("mr id: %lld\n", (long long int)next_mr->addr);

        // get memory size
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_mr->sz = strtoull(l_buf, NULL, 10);
        DEBUG_PRINT("mr sz: %lu\n", next_mr->sz);

        // check for correct memory size
        if (next_mr->sz == 0 || next_mr->sz % TARGET_PAGE_SIZE != 0) {
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
        heca.mr_count++;
    }

    // Now, we setup the mr_array with the unmap_data structs created above
    heca.mr_array = calloc(heca.mr_count, sizeof(struct unmap_data));
    for (i = 0; i < heca.mr_count; i++) {
        memcpy(&heca.mr_array[i], g_slist_nth_data(mr_list, i),
                sizeof(struct unmap_data));
    }

    g_slist_free(mr_list);
}

void parse_heca_client_commandline(const char* optarg) 
{
    printf("parse_heca_client_commandline\n");
    heca.dsm_id = get_param_int("dsmid", optarg);
    printf("dsm_id = %d\n", heca.dsm_id);

    DEBUG_PRINT("dsm_id = %d\n", heca.dsm_id);

    heca.local_svm_id = get_param_int("vmid", optarg);
    DEBUG_PRINT("local_svm_id = %d\n", heca.local_svm_id);
    printf("local_svm_id= %d\n", heca.local_svm_id);

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
    if (port) /* FIXME: port not needed */
        DEBUG_PRINT("port is : %d\n",port);

    // Parse tcp port
    q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
    q++;
    int tcp_port = strtoull(l_buf, NULL, 10);
    DEBUG_PRINT("tcp port: %d\n", tcp_port);

    bzero((char*) &heca.master_addr, sizeof(heca.master_addr));
    heca.master_addr.sin_family = AF_INET;
    heca.master_addr.sin_port = htons(tcp_port);
    heca.master_addr.sin_addr.s_addr = inet_addr(ip);
    printf("leaving...\n");
}

void heca_migrate_dest_init(const char* dest_ip, const char* source_ip)
{
    heca_config();
    heca.is_enabled = true;
    heca.dsm_id = 1;          // only need 1 for live migration (LM)
    heca.local_svm_id = 1;    // master node is 1
    heca.svm_count = 2;       // only master and client required for LM
    heca.mr_count = 1;        // only need 1 memory region for LM

    struct svm_data dst_svm = {
        .dsm_id = 1,
        .svm_id = 1,
        .server = {
            .sin_addr.s_addr = inet_addr(dest_ip),
            .sin_port = htons(heca.rdma_port)
        }
    };
    struct svm_data src_svm = {
        .dsm_id = 1,
        .svm_id = 2,
        .server = {
            .sin_addr.s_addr = inet_addr(source_ip),
            .sin_port = htons(heca.rdma_port)
        }
    };

    heca.svm_array = calloc(heca.svm_count, sizeof(struct svm_data));
    heca.svm_array[0] = dst_svm;
    heca.svm_array[1] = src_svm;

    heca.mr_array = calloc(heca.mr_count, sizeof(struct unmap_data));
    struct unmap_data mr = {
        .dsm_id = 1,
        .mr_id = 1,
        .svm_ids = { 2, 0 },
        .flags = UD_COPY_ON_ACCESS
    };
    heca.mr_array[0] = mr;

    void *ram_ptr = heca_get_system_ram_ptr();
    if (!ram_ptr) {
        DEBUG_PRINT("Error getting ram_ptr to system memory\n");
        exit(1);
    }
    uint64_t ram_sz = heca_get_system_ram_size();

    heca.mr_array[0].addr = ram_ptr; // only one memory region required for LM
    heca.mr_array[0].sz = ram_sz;

    DEBUG_PRINT("initializing heca master\n");

    //print_data_structures();
    heca.rdma_fd = heca_master_open(heca.svm_count, 
            heca.svm_array, heca.mr_count, heca.mr_array);

    if (heca.rdma_fd < 0) {
        DEBUG_PRINT("Error initializing master node\n");
        exit(1);
    }

    DEBUG_PRINT("Heca master node is ready..\n");
    //dsm_cleanup(fd); 
}

void heca_migrate_src_init(const char* uri, int precopy_time)
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

    heca_start_mig_timer(precopy_time);

    void *ram_ptr = heca_get_system_ram_ptr();
    uint64_t ram_size = heca_get_system_ram_size();
    if (!ram_ptr) {
        DEBUG_PRINT("Error getting ram pointer\n");
        exit(1);
    }

    DEBUG_PRINT("initializing heca client node ...\n");
    
    heca.rdma_fd = heca_client_open(ram_ptr, ram_size, 
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
    char char_array_uri[256];

    char_array_uri[sizeof(char_array_uri) - 1] = 0;
    strncpy(char_array_uri, uri, sizeof(char_array_uri) - 1);

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

static inline int heca_assign_master_mem(void *ram_ptr, uint64_t ram_size)
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
            inet_ntoa(heca.svm_array[i].server.sin_addr),
            ntohs(heca.svm_array[i].server.sin_port));
    }
    printf("mr_array:\n");
    for (i = 0; i < heca.mr_count; i++) {
        printf("{ .dsm_id = %d, .mr_id = %d, .addr = %ld, .sz = %lld, .flags = %d, .svm_ids = { ",
            heca.mr_array[i].dsm_id, heca.mr_array[i].mr_id, 
            (unsigned long) heca.mr_array[i].addr, 
            (long long) heca.mr_array[i].sz, heca.mr_array[i].flags);
        j = 0;
        while(heca.mr_array[i].svm_ids[j] != 0) {
            printf("%d, ", heca.mr_array[i].svm_ids[j]);
            j++;
        }
        printf("0 } }\n");
    }
}

static inline MemoryRegion *heca_get_system_mr(void)
{
    RAMBlock *block;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (strncmp(block->idstr, "pc.ram", strlen(block->idstr)) == 0)
            return block->mr; 
    }
    return NULL;
}

void *heca_get_system_ram_ptr(void)
{
    MemoryRegion *sys_mr = heca_get_system_mr();
    if (sys_mr)
        return memory_region_get_ram_ptr(sys_mr);
    return NULL;
}

uint64_t heca_get_system_ram_size(void)
{
    MemoryRegion *sys_mr = heca_get_system_mr();
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
    
    DEBUG_PRINT("Starting to pull all pages to local node.\n");

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
    DEBUG_PRINT("Finished reading ram, please terminate the source node.\n");
    /* TODO: Send a message to the source to self terminate */

    pthread_exit(NULL);
}


void heca_touch_all_ram(void)
{
    pthread_t t;
    pthread_create(&t, NULL, touch_all_ram_worker, NULL);
}


int heca_unmap_memory(void* addr, size_t size)
{
    int ret = 0;

    // create unmap object for dirty range and unmap it

    struct unmap_data unmap_region;
    unmap_region.addr = addr;
    unmap_region.mr_id = 0; 
    unmap_region.sz = size;
    unmap_region.dsm_id = 1;        // Always 1 in LM
    unmap_region.svm_ids[0] = 2;    // Always set to 2 in LM
    unmap_region.svm_ids[1] = 0;

    /* FIXME - externd linux-heca and libheca */
    ret = ioctl(heca.rdma_fd, HECAIOC_MR_UNMAP, &unmap_region);
    if (ret)
        return -1;
    else
        return ret;
    return -1;
}

static void mig_timer_expired(void *opaque)
{
    heca.is_timer_expired = true;
    qemu_del_timer(heca.migration_timer);
}

void heca_start_mig_timer(uint64_t timeout) 
{
    // Start timer with timeout value and mig_timer_expired callback
    heca.migration_timer = qemu_new_timer_ms(rt_clock, mig_timer_expired, NULL);
    qemu_mod_timer(heca.migration_timer, qemu_get_clock_ms(rt_clock) + timeout);
}

bool heca_is_mig_timer_expired(void)
{
    return heca.is_timer_expired;
}

void heca_set_post_copy_phase(void)
{
    heca.is_iterative_phase = false;
}

bool heca_is_pre_copy_phase(void)
{
    return heca.is_iterative_phase;
}

int heca_unmap_dirty_bitmap(uint8_t *bitmap, uint32_t bitmap_size)
{
    unsigned long host_ram;
    int i, ret = 0;
    void * unmap_addr = NULL;

    host_ram = (unsigned long) heca_get_system_ram_ptr();
 
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
            ret = heca_unmap_memory(unmap_addr, unmap_size);
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
        ret = heca_unmap_memory(unmap_addr, unmap_size);
        if (ret < 0) {

            return ret;
        }
    }
    heca_touch_all_ram();

    return ret;
}

void heca_master_cmdline_init(const char* optarg)
{
    heca.is_enabled = true;
    heca.is_master = true;
    parse_heca_master_commandline(optarg);

    int i;
    for (i = 0; i < heca.mr_count; i++)
        heca.mr_array[i].flags |= UD_AUTO_UNMAP;
}

void heca_client_cmdline_init(const char* optarg)
{
    heca.is_enabled = true;
    heca.is_master = false;
    parse_heca_client_commandline(optarg);
}

void heca_init(void* ram_ptr, uint64_t ram_size)
{
    if (heca.is_master) {
        bool debug = true;
        if (debug) 
            print_data_structures();

        heca_assign_master_mem(ram_ptr, ram_size);

        // init heca
        heca.rdma_fd = heca_master_open(heca.svm_count, 
                heca.svm_array, heca.mr_count, heca.mr_array);
    } else {
        heca.rdma_fd = heca_client_open(ram_ptr, ram_size, 
                heca.local_svm_id, &heca.master_addr);
    }

    if (heca.rdma_fd < 0) {
        DEBUG_PRINT("Error initializing master node\n");
        exit(1);
    }

    DEBUG_PRINT("Heca is ready...\n");

}


