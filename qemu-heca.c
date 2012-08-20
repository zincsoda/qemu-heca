#include <stdio.h>
#include "qemu-heca.h"
#include "libheca.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PAGE_SIZE 4096

int heca_enabled = 0;
int heca_is_master = 0;

void qemu_heca_init(void) {

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
