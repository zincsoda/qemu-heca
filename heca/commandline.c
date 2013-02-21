#include "heca/commandline.h"

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

/* setup data for qemu_heca_init to setup master and slave nodes */
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
    char h_buf[200];
    char l_buf[200];
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
        DEBUG_PRINT("ip is: %s\n", next_svm->ip);

        // Parse rdma port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        next_svm->server.sin_port = htons(strtoull(l_buf, NULL, 10));
        DEBUG_PRINT("port is: %d\n", next_svm->port);

        // Parse tcp port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        tcp_port = strtoull(l_buf, NULL, 10);
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
