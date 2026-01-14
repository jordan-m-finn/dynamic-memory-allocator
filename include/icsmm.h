/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them in
 * helpers.h
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */
#ifndef ICSMM_H
#define ICSMM_H
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_MAGIC 0x100DECAFBEE5UL
#define FOOTER_MAGIC 0x0A011DABUL

#define REQUEST_SIZE_BITS 16
#define HID_SIZE_BITS 48
#define BLOCK_SIZE_BITS 16
#define FID_SIZE_BITS 32

#define BUCKET_COUNT 5

typedef struct __attribute__((__packed__)) {
    uint64_t     block_size : BLOCK_SIZE_BITS;
    uint64_t            hid : HID_SIZE_BITS;
} ics_header;

typedef struct __attribute__((__packed__)) ics_free_header {
  ics_header header;
  struct ics_free_header *next;
  struct ics_free_header *prev;
} ics_free_header;

typedef struct __attribute__((__packed__)) ics_footer {
    uint64_t     block_size : BLOCK_SIZE_BITS;
    uint64_t requested_size : REQUEST_SIZE_BITS;
    uint64_t            fid : FID_SIZE_BITS;
} ics_footer;

typedef struct ics_bucket {
        size_t max_size;
        ics_free_header *freelist_head;
} ics_bucket;

extern ics_bucket *seg_buckets;

void *ics_malloc(size_t size);
int ics_free(void *ptr);

void ics_mem_init(size_t bounds[]);
void ics_mem_fini();
void *ics_get_brk();
void *ics_inc_brk();
void ics_buckets_print();
void ics_freelist_print(int index);
int ics_header_print(void *header);
int ics_payload_print(void *payload);

void ics_buckets_print_compact();
void ics_freelist_print_compact(int index);
int ics_header_print_compact(void *header);
int ics_payload_print_compact(void *payload);

#endif
