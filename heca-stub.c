#include "heca.h"

void heca_init(void* ram_ptr, uint64_t ram_size)
{
}

void heca_migrate_dest_init(const char* dest_ip, const char* source_ip)
{
}

void heca_migrate_src_init(const char* dest_ip, int precopy_time)
{
}

void heca_set_post_copy_phase(void)
{
}

bool heca_is_mig_timer_expired(void)
{
}

void heca_master_cmdline_init(const char* optarg)
{
}

void heca_client_cmdline_init(const char* optarg)
{
}

bool heca_is_master(void)
{
    return false;
}

bool heca_is_enabled(void)
{
    return false;
}

int heca_unmap_dirty_bitmap(uint8_t *bitmap, uint32_t bitmap_size)
{
    return -1;
}

bool heca_is_pre_copy_phase(void)
{
    return false;
}

