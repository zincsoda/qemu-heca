#include <stdio.h>
#include "qemu-heca.h"
#include "libheca.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PAGE_SIZE 4096



int heca_enabled = 0;
int heca_is_master = 0;


void qemu_heca_init(void) 
{

    if (heca_is_master) {
        
        printf("initializing heca master\n");
        
        int fd;
        unsigned long dsm_mem_sz = PAGE_SIZE * 1000;
        void *dsm_mem = valloc(dsm_mem_sz);
        int svm_count;
        int mr_count;
        
        svm_count = 2;
        struct svm_data *svm_array = calloc(svm_count, sizeof(struct svm_data));    
        struct svm_data node1 = { .dsm_id = 1, .svm_id = 1, .offset = 0, .ip = "192.168.0.6", .port = 4444 };
        struct svm_data node2 = { .dsm_id = 1, .svm_id = 2, .offset = 0, .ip = "192.168.0.7", .port = 4444 };
        svm_array[0] = node1;
        svm_array[1] = node2;
        
        mr_count = 1;
        struct unmap_data *unmap_array = calloc(1, sizeof(struct unmap_data));
        struct unmap_data u1 = { .dsm_id = 1, .addr = 0, .sz = dsm_mem_sz, .svm_ids = {1, 0}, .unmap = UNMAP_REMOTE };
        unmap_array[0] = u1;
        
        fd = dsm_master_init(dsm_mem, dsm_mem_sz, svm_count, svm_array, mr_count, unmap_array);
        if (fd < 0) {
            printf("Error initializing master node\n");
            exit(1);
        }
        
        printf("Heca is ready..\n");

        //dsm_cleanup(fd);
        
        //free(svm_array);
        //free(unmap_array);
        
    } else {
        printf("initializing heca client\n");

        int fd;
        unsigned long dsm_mem_sz;
        void *dsm_mem;
        int local_svm_id;
        struct sockaddr_in master_addr;
        
        dsm_mem_sz = PAGE_SIZE * 1000;
        dsm_mem = valloc(dsm_mem_sz);
        local_svm_id = 2; // need to read this from config
        bzero((char*) &master_addr, sizeof(master_addr));
        master_addr.sin_family = AF_INET;
        master_addr.sin_port = htons(4445);
        master_addr.sin_addr.s_addr = inet_addr("192.168.0.6");
        
        // dsm init
        fd = dsm_client_init (dsm_mem, dsm_mem_sz, local_svm_id, &master_addr);
        if (fd < 0 ) {
            printf("Error initializing client node\n");
            exit(1);
        }

        printf("Heca is ready..\n");

        //dsm_cleanup(fd); 
 
    }
}

void qemu_heca_parse_commandline(const char* optarg)
{
    char nodeinfo_option[128];
    struct dsm_vm_info *dsm_vm_temp;

    dsm_data = g_malloc(sizeof(struct dsm_info_data));
    dsm_data->type = type;
    dsm_data->node_size = 0;

    dsm_data->dsm_id = get_param_int("dsmid", optarg);
    dsm_data->vm_id = get_param_int("vmid", optarg);

    get_param(nodeinfo_option, "vminfo", sizeof(nodeinfo_option), optarg);
    const char *p = nodeinfo_option;
    char h_buf[200];
    char l_buf[200];
    const char *q;

    QLIST_INIT(&dsm_data->all_vm);

    while (*p != '\0') {
        struct dsm_vm_info *other_vm_temp = 
            g_malloc0(sizeof(struct dsm_vm_info));

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = h_buf;

        // Parse vm id
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        other_vm_temp->vm_id = strtoull(l_buf, NULL, 10);
        if ((other_vm_temp->vm_id & 0xFFFF ) != other_vm_temp->vm_id)
        {
            fprintf(stderr, "[DSM] Invalid vm_id: %d\n",
                (int)other_vm_temp->vm_id);
            exit(1);
        }
        printf("vm id is : %d\n",other_vm_temp->vm_id);

        // Parse node IP
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        strcpy(other_vm_temp->ip, l_buf);
        printf("ip is : %s\n",other_vm_temp->ip);

        // Parse rdma port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        other_vm_temp->rdma_port = strtoull(l_buf, NULL, 10);
        printf("rdma port is : %d\n",other_vm_temp->rdma_port);

        // Parse tcp port
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        strcpy(other_vm_temp->tcp_port, l_buf);
        printf("tcp port is : %s\n",other_vm_temp->tcp_port);

        other_vm_temp->sock = -1;

        insert_new_vm(other_vm_temp);
    }

    get_param(nodeinfo_option, "mr", sizeof(nodeinfo_option), optarg);
    p = nodeinfo_option;

    while (*p != '\0') {
        struct mem_region *mr_tmp = g_malloc0(sizeof(struct mem_region));

        p = get_opt_name(h_buf, sizeof(h_buf), p, '#');
        p++;
        q = h_buf;

        // get memory start offset
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        mr_tmp->vm_memory_start = strtoull(l_buf, NULL, 10);

        // get memory size
        q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
        q++;
        mr_tmp->vm_memory_size = strtoull(l_buf, NULL, 10);

        // check for correct memory size
        if (mr_tmp->vm_memory_size % TARGET_PAGE_SIZE != 0) {
            fprintf(stderr, "DSM: Wrong mem size. \n \
                It has to be a multiple of %d\n", (int)TARGET_PAGE_SIZE);
            exit(1);
        }

        // for all vmid's search the vm and add mr
        while (*q != '\0') {
            uint32_t *vmid = g_malloc0(sizeof(uint32_t));
            struct dsm_vm_info *dsm_vm;
            
            q = get_opt_name(l_buf, sizeof(l_buf), q, ':');
            if (strlen(q))
                q++;
            *vmid = strtoull(l_buf, NULL, 10);
            
            dsm_vm = search_vm(*vmid);
            insert_new_mr(dsm_vm, mr_tmp);
        }
    }

    printf("VM's registered: \n");
    QLIST_FOREACH(dsm_vm_temp,&dsm_data->all_vm,dsm_vm_info_list_item)
        printf("vmid: %d\n",dsm_vm_temp->vm_id);

    dsm_data->rdma_fd = open("/dev/rdma", O_RDWR);
    if (!dsm_data->rdma_fd)
        printf("Creating the fdes failed.\n");   
}
