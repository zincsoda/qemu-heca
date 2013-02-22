#ifndef HECA_CMDLINE_H_
#define HECA_CMDLINE_H_

#define DEBUG 1

#include <libheca.h>

//#include "qemu-option.h"
#include "monitor.h"
#include "heca/qemu-heca.h"


void parse_heca_master_commandline(const char* optarg);
void parse_heca_client_commandline(const char* optarg);

#endif /* HECA_CMDLINE_H_ */
