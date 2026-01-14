#ifndef HELPERS4_H
#define HELPERS4_H

#include <stddef.h>
#include <stdint.h>
#include "icsmm.h"

// A header file for helpers.c
// Declare any additional functions in this file

// size calc
/*
 * calc the total block size needed for a requested payload size
 * includes header (8), payload, padding and footer (8)
 * ensures minimum block size of 32 and 16-byte alignment
 */
size_t calculate_block_size(size_t requested_size);

// align size up to nearest multiple of 16 bytes
size_t align_to_16(size_t size);

// extract block size from header (removes allocated bit)
uint64_t get_size_from_header(ics_header *header);

// check if block is allocated by examining header
int is_allocated_header(ics_header *header);

/*
 * set header with block_size and allocated bit
 * also sets hid to HEADER_MAGIC
 */
void set_header(ics_header *header, uint64_t block_size, int allocated);

/* ========= FOOTER OPS ======== */

// extract block size from footer (removes allocated bit)
uint64_t get_size_from_footer(ics_footer *footer);

// check if block is allocated by examining footer
int is_allocated_footer(ics_footer *footer);

/*
 * set footer with block_size, requested_size, and allocated bit
 * also sets fid to FOOTER_MAGIC
 */
void set_footer(ics_footer *footer, uint64_t block_size, uint64_t requested_size, int allocated);

/* ========= BLOCK NAV ========= */

// get header pointer from payload pointer
ics_header *get_header_from_payload(void *payload);

// get payload pointer from header pointer
void *get_payload_from_header(ics_header *header);

// get footer pointer from header pointer
ics_footer *get_footer_from_header(ics_header *header);

// get header of next block in memory
ics_header *get_next_header(ics_header *current_header);

// get footer of prev block in memory
ics_footer *get_prev_footer(ics_header *current_header);

// get header of prev block in memory (using its footer)
ics_header *get_prev_header(ics_header *current_header);

/* ========= BLOCK VALIDATION ======== */

/*
 * check if a pointer is valid for freeing
 * returns bucket index (0-4)
 */
int find_bucket_index(size_t block_size);

// insert a free block into a bucket's freelist (LIFO - at head)
void insert_free_block(ics_free_header *block, int bucket_index);

// remove a free block form its bucket's freelist
void remove_free_block(ics_free_header *block, int bucket_index);

/*
 * search for a free block that fits the requested size
 * uses first-fit policy within buckets
 * returns NULL if no fit found
 */
ics_free_header *find_fit(size_t block_size);

/*
 * check if a pointer is valid for freeing
 * returns 1 if valid, 0 if invalid
 */
int is_valid_free_pointer(void *ptr);

#endif
