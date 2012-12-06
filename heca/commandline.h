#ifndef HECA_CMDLINE_H_
#define HECA_CMDLINE_H_

#include <libheca.h>
#include <dsm_init.h>

//#include "qemu-option.h"
#include "monitor.h"
#include "heca/qemu-heca.h"

#define DEBUG 1

void parse_heca_master_commandline(const char* optarg);
void parse_heca_client_commandline(const char* optarg);

#endif /* HECA_CMDLINE_H_ */
