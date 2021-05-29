/* -*- C -*- */
/*
 */

#pragma once

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include <stddef.h> 

void *mio_mem_alloc(size_t size);
void mio_mem_free(void *p);
void mio_memset(void *p, int c, size_t size);
void mio_mem_copy(void *to, void *from, size_t size);

uint64_t mio_now();
uint64_t mio_time_seconds(uint64_t time_in_nanosecs);
uint64_t mio_time_nanoseconds(uint64_t time_in_nanosecs);

uint64_t mio_byteorder_cpu_to_be64(uint64_t cpu_64bits);
uint64_t mio_byteorder_be64_to_cpu(uint64_t big_endian_64bits);
uint16_t mio_byteorder_cpu_to_le16(uint16_t cpu_16bits);
uint16_t mio_byteorder_le16_to_cpu(uint16_t le_16bits);
uint32_t mio_byteorder_cpu_to_le32(uint32_t cpu_32bits);
uint32_t mio_byteorder_le32_to_cpu(uint32_t le_32bits);
uint64_t mio_byteorder_cpu_to_le64(uint64_t cpu_64bits);
uint64_t mio_byteorder_le64_to_cpu(uint64_t le_64bits);

#endif /* __UTILS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
