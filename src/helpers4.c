#include "helpers4.h"
#include "debug.h"
#include <errno.h>

extern ics_bucket *seg_buckets;
extern void *ics_get_brk();

/* Helper function definitions go here */

/* ======== SIZE CALC ======== */

size_t align_to_16(size_t size) {
	// round up to nearest multiple of 16
	return (size + 15) & ~0xF;
}

size_t calculate_block_size(size_t requested_size) {
	// min payload size is 16 bytes (for next/prev pointers in free blocks)
	size_t payload_size = requested_size < 16 ? 16 : requested_size;

	// header (8)
	size_t total_size = 8 + payload_size + 8;

	// align to 16 bytes
	size_t aligned_size = align_to_16(total_size);

	// ensure min block size of 32 bytes
	if (aligned_size < 32) {
		aligned_size = 32;
	}

	return aligned_size;
}

/* ====== HEADER OPS ======== */

uint64_t get_size_from_header(ics_header *header) {
	// mask off the allocated bit (LSB) to get actual block size
	return header->block_size & ~0x1;
}

int is_allocated_header(ics_header *header) {
	// check lsb of block_size
	return header->block_size & 0x1;
}

void set_header(ics_header *header, uint64_t block_size, int allocated) {
	// set block_size with allocated bit
	header->block_size = block_size | (allocated ? 1 : 0);

	// set header magic num
	header->hid = HEADER_MAGIC;
}

/* ======== FOOTER OPS ======== */

uint64_t get_size_from_footer(ics_footer *footer) {
	// mask off the allocated bit (LSB) to get actual block size
	return footer->block_size & ~0x1;
}

int is_allocated_footer(ics_footer *footer) {
	// check lsb of block_size
	return footer->block_size & 0x1;
}

void set_footer(ics_footer *footer, uint64_t block_size, uint64_t requested_size, int allocated) {
	// set block_size with allocated bit
	footer->block_size = block_size | (allocated ? 1 : 0);

	// set requested size
	footer->requested_size = requested_size;

	footer->fid = FOOTER_MAGIC;
}

/* ========== BLOCK NAV ========== */
ics_header *get_header_from_payload(void *payload) {
	// 8 bytes prior to payload
	return (ics_header *)((char*)payload - 8);
}

void *get_payload_from_header(ics_header *header) {
	// payload starts at 8 bytes after header obv
	return (void *)((char*)header + 8);
}

ics_footer *get_footer_from_header(ics_header *header) {
	// get block size w/o allocated bit
	uint64_t block_size = get_size_from_header(header);

	// footer is at: header + block_size - 8 (footer size)
	return (ics_footer *)((char *)header + block_size - 8);
}

ics_header *get_next_header(ics_header *current_header) {
	// get the block_size
	uint64_t block_size = get_size_from_header(current_header);

	// next header is at: current header + block size
	return (ics_header *)((char *)current_header + block_size);
}

ics_footer *get_prev_footer(ics_header *current_header) {
	// prev block's footer is 8 bytes prior to current header
	return (ics_footer *)((char *)current_header - 8);
}

ics_header *get_prev_header(ics_header *current_header) {
	// get prev block's footer
	ics_footer *prev_footer = get_prev_footer(current_header);

	// get the prev block's size
	uint64_t prev_block_size = get_size_from_footer(prev_footer);

	// prev header is at: current_header - prev_block_size
	return (ics_header *)((char *)current_header - prev_block_size);
}

/* =========== BLOCK VALIDATION ============ */

int is_valid_free_pointer(void *ptr) {
	// this will be implemented fully in icsmm.c since it needs heap bounds
	// placehold NULL for now
	if (ptr == NULL) {
		return 0;
	}

	ics_header *header = get_header_from_payload(ptr);

	// check if allocated bit is set in header
	if (!is_allocated_header(header)) {
		return 0;
	}

	// check hid
	if (header->hid != HEADER_MAGIC) {
		return 0;
	}

	// get footer and check it
	ics_footer *footer = get_footer_from_header(header);

	// check fid
	if (footer->fid != FOOTER_MAGIC) {
		return 0;
	}

	// check if allocated bit is set in footer
	if (!is_allocated_footer(footer)) {
		return 0;
	}

	// check if block sizes match
	if (get_size_from_header(header) != get_size_from_footer(footer)) {
		return 0;
	}

	return 1;
}

/* ================ BUCKET MANAGEMENT ================= */

int find_bucket_index(size_t block_size) {
	// seg_buckets must be initialized before calling this
	if (seg_buckets == NULL) {
		return -1;
	}

	// search for the appropriate bucket
	// buckets hold blocks in ranges: (0, max_size]
	int i;
	for (i = 0; i < BUCKET_COUNT; i++) {
		if (block_size <= seg_buckets[i].max_size) {
			return i;
		}
	}

	// should never reach here if max_size of last bucket is large enough
	return BUCKET_COUNT - 1;
}

void insert_free_block(ics_free_header *block, int bucket_index) {
	// validate inputs
	if (block == NULL || bucket_index < 0 || bucket_index >= BUCKET_COUNT) {
		return;
	}

	if (seg_buckets == NULL) {
		return;
	}

	// LIFO insertion - insert at the head of the list
	ics_free_header *old_head = seg_buckets[bucket_index].freelist_head;

	// set block's next to point to old head
	block->next = old_head;

	//  set block's prev to NULL (it's now the head)
	block->prev = NULL;

	// if there was an old head, update its prev pointer
	if (old_head != NULL) {
		old_head->prev = block;
	}

	// update bucket's head to point to new block
	seg_buckets[bucket_index].freelist_head = block;
}

void remove_free_block(ics_free_header *block, int bucket_index) {
	// validate inputs
	if (block == NULL || bucket_index < 0 || bucket_index >= BUCKET_COUNT) {
		return;
	}

	if (seg_buckets == NULL) {
		return;
	}

	// update the prev block's next pointer (or bucket head)
	if (block->prev != NULL) {
		// not the head of the list
		block->prev->next = block->next;
	} else {
		// this is the head of the list
		seg_buckets[bucket_index].freelist_head = block->next;
	}

	// update the next block's prev pointer
	if (block->next != NULL) {
		block->next->prev = block->prev;
	}

	// clear the block's pointers (optional, but good practice)
	block->next = NULL;
	block->prev = NULL;
}

ics_free_header *find_fit(size_t block_size) {
	if (seg_buckets == NULL) {
		return NULL;
	}

	// find the starting bucket
	int bucket_idx = find_bucket_index(block_size);

	if (bucket_idx < 0) {
		return NULL;
	}

	// search starting from the appropriate bucket
	int i;
	for (i = bucket_idx; i < BUCKET_COUNT; i++) {
		ics_free_header *current = seg_buckets[i].freelist_head;

		// first-fit search w/in this bucket
		while (current != NULL) {
			uint64_t current_size = get_size_from_header(&(current->header));

			if (current_size >= block_size) {
				// found a fit
				return current;
			}

			current = current->next;
		}
	}

	// no fit found in any bucket
	return NULL;
}
