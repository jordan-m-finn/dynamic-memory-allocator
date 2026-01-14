#include "icsmm.h"
#include "debug.h"
#include "helpers4.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* ====== GLOBAL VARs ====== */
static void *heap_start = NULL; // start of heap, after prologue
static void *heap_end = NULL; // current end of heap aka brk pointer
static int heap_initialized = 0; // flag to track if heap is init
static int pages_allocated = 0; // track num of pages alloc'd (max 6)

#define PAGE_SIZE 4096
#define MAX_PAGES 6

/* ======= FWD DECLARATIONS ====== */
static int init_heap(void);
static void *extend_heap(size_t size);
static void place_block(ics_free_header *block, size_t adjusted_size);
static void *coalesce(ics_header *header);

/* ======= HEAP INIT ======== */

/*
 * initialize the heap with prologue, epilogue, and init free block
 *
 * heap structure after init:
 * [prologue: 8-byte footer, alloc'd]
 * [initial free block]
 * [epilogue: 8-byte header, alloc'd]
 *
 * @return 0 on success, -1 on failure
 */
static int init_heap(void) {
	// req 1st page from the system
	void *page = ics_inc_brk();
	if (page == (void *)-1) {
		errno = ENOMEM;
		return -1;
	}

	pages_allocated = 1;
	heap_start = page;
	heap_end = ics_get_brk();

	// calc space available
	// total: 4096 bytes
	// prologue: 8 bytes (footer only)
	// epilogue: 8 bytes (header only)
	// remaining for free block: 4096 - 16 = 4080 bytes
	
	char* current = (char *)heap_start;

	// set up prologue (8-byte footer, alloc'd, size = 0)
	ics_footer *prologue = (ics_footer *)current;
	set_footer(prologue, 0, 0, 1); // size 0, alloc'd
	current += 8;

	// calc size of initial free block
	// total space - prologue (8) - epilogue (8) = 4080
	size_t free_block_size = PAGE_SIZE - 16;

	// set up initial free block
	ics_header *free_header = (ics_header *)current;
	set_header(free_header, free_block_size, 0); // not alloc'd
	
	ics_footer *free_footer = get_footer_from_header(free_header);
	set_footer(free_footer, free_block_size, 0, 0); // not alloc'd, req size = 0
	
	current += free_block_size;

	// set up epilogue (8-byte header, alloc'd, size = 0)
	ics_header *epilogue = (ics_header *)current;
	set_header(epilogue, 0, 1);

	// insert the initial free block into the appropriate bucket
	int bucket_idx = find_bucket_index(free_block_size);
	if (bucket_idx >= 0) {
		ics_free_header *free_block = (ics_free_header *)free_header;
		free_block->next = NULL;
		free_block->prev = NULL;
		insert_free_block(free_block, bucket_idx);
	}

	heap_initialized = 1;
	return 0;
}

/* ============= HEAP EXTENSION ============ */

/*
 * extend the heap by req more pages
 * creates a new free block and coalesces with previous block if it's free
 *
 * @param size The min number of bytes needed
 * @return Pointer to the new free block's header, or NULL on failure
 */
static void *extend_heap(size_t size) {
	// calc the actual space we need to add
	// account for any existing free space at the end that will be coalesced
	//
	// so first, check if the last block before epilogue is free
	void *epilogue_ptr = (char *)heap_end - 8;
	ics_header *epilogue = (ics_header *)epilogue_ptr;

	// get the block before the epilogue
	ics_footer *last_footer = get_prev_footer(epilogue);
	size_t last_block_free_space = 0;

	if (!is_allocated_footer(last_footer)) {
		// last block is free, we can coalesce with it
		last_block_free_space = get_size_from_footer(last_footer);
	}

	// calc how much more space we actually need
	size_t additional_space_needed;
	if (size > last_block_free_space) {
		additional_space_needed = size - last_block_free_space;
	} else {
		// therefore, we already have enough space (just in case)
		additional_space_needed = 0;
	}	
	
	// calc num of pages needed
	size_t pages_needed = (additional_space_needed + PAGE_SIZE - 1) / PAGE_SIZE;

	// make sure we req at least 1 page
	if (pages_needed == 0) {
		pages_needed = 1;
	}

	// check if we would exceed the max
	if (pages_allocated + pages_needed > MAX_PAGES) {
		errno = ENOMEM;
		return NULL;
	}

	// req the pages
	void* old_brk = heap_end;

	size_t i;
	for (i = 0; i < pages_needed; i++) {
		void *new_page = ics_inc_brk();
		if (new_page == (void *)-1) {
			errno = ENOMEM;
			return NULL;
		}
		pages_allocated++;
	}

	heap_end = ics_get_brk();

	// calc the size of new free block
	size_t new_block_size = (char *)heap_end - (char *)old_brk;

	// the old epilogue is now where we start our new block
	// we need to overwrite the old epilogue
	ics_header *old_epilogue = (ics_header *)((char *)old_brk - 8);

	// create new free block header where old epilogue was
	ics_header *new_header = old_epilogue;
	set_header(new_header, new_block_size, 0);

	ics_footer *new_footer = get_footer_from_header(new_header);
	set_footer(new_footer, new_block_size, 0, 0); // not alloc'd
	
	// create new epilogue at the end
	ics_header *new_epilogue = (ics_header *)((char *)heap_end - 8);
	set_header(new_epilogue, 0, 1); // size 0, alloc'd
	
	// coalesce with prev block if it's free
	return coalesce(new_header);
}

/* ========== COALESCE =========== */

/*
 * coalesce adjacent free blocks
 * implements the 4 cases from fig 9.40 in the textbook
 *
 * @param header Pointer to the header of the block to coalesce
 * @return Pointer to the header of the coalesced block
 */
static void *coalesce(ics_header *header) {
	// get adjacent blocks
	ics_footer *prev_footer = get_prev_footer(header);
	ics_header *next_header = get_next_header(header);

	int prev_allocated = is_allocated_footer(prev_footer);
	int next_allocated = is_allocated_header(next_header);

	uint64_t current_size = get_size_from_header(header);

	// case 1: both adjacent blocks are alloc'd
	if (prev_allocated && next_allocated) {
		// no coalescing needed, just insert into free list
		int bucket_idx = find_bucket_index(current_size);
		insert_free_block((ics_free_header *)header, bucket_idx);
		return header;
	}

	// case 2: prev alloc'd, next free
	else if (prev_allocated && !next_allocated) {
		// remove next block from its free list
		ics_free_header *next_block = (ics_free_header *)next_header;
		uint64_t next_size = get_size_from_header(next_header);
		int next_bucket =find_bucket_index(next_size);
		remove_free_block(next_block, next_bucket);

		// merge current and next
		uint64_t new_size = current_size + next_size;
		set_header(header, new_size, 0);

		ics_footer *new_footer = get_footer_from_header(header);
		set_footer(new_footer, new_size, 0, 0);

		// insert merged block
		int bucket_idx = find_bucket_index(new_size);
		insert_free_block((ics_free_header *)header, bucket_idx);
		return header;
	}

	// case 3: prev free, next alloc'd
	else if (!prev_allocated && next_allocated) {
		// get prev block
		ics_header *prev_header = get_prev_header(header);
		ics_free_header *prev_block = (ics_free_header *)prev_header;
		uint64_t prev_size = get_size_from_header(prev_header);

		// remove prev blocck from its free list
		int prev_bucket = find_bucket_index(prev_size);
		remove_free_block(prev_block, prev_bucket);

		// merge prev and current
		uint64_t new_size = prev_size + current_size;
		set_header(prev_header, new_size, 0);

		ics_footer *new_footer = get_footer_from_header(prev_header);
		set_footer(new_footer, new_size, 0, 0);

		// insert merged block
		int bucket_idx = find_bucket_index(new_size);
		insert_free_block((ics_free_header *)prev_header, bucket_idx);
		return prev_header;
	}

	// case 4: both adjacent blocks are free
	else {
		// get both adjacent blocks
		ics_header *prev_header = get_prev_header(header);
		ics_free_header *prev_block = (ics_free_header *)prev_header;
		uint64_t prev_size = get_size_from_header(prev_header);

		ics_free_header *next_block = (ics_free_header *)next_header;
		uint64_t next_size = get_size_from_header(next_header);

		// remove both from their free lists
		int prev_bucket = find_bucket_index(prev_size);
		remove_free_block(prev_block, prev_bucket);

		int next_bucket = find_bucket_index(next_size);
		remove_free_block(next_block, next_bucket);

		// merge all three blocks
		uint64_t new_size = prev_size + current_size + next_size;
		set_header(prev_header, new_size, 0);

		ics_footer *new_footer = get_footer_from_header(prev_header);
		set_footer(new_footer, new_size, 0, 0);

		// insert merged block
		int bucket_idx = find_bucket_index(new_size);
		insert_free_block((ics_free_header *)prev_header, bucket_idx);
		return prev_header;
	}
}

/* =========== BLOCK PLACEMENT & SPLITTING ============ */

/*
 * place an allocated block in a free block
 * splits the block if remainder is >= 32 bytes (no splinters)
 *
 * @param block The free block to place into
 * @param adjusted_size The size needed (including header/footer)
 */
static void place_block(ics_free_header *block, size_t adjusted_size) {
	ics_header *header = &(block->header);
	uint64_t total_size = get_size_from_header(header);

	// remove from free list first
	int bucket_idx = find_bucket_index(total_size);
	remove_free_block(block, bucket_idx);
	
	// check if we should split
	size_t remainder = total_size - adjusted_size;

	if (remainder >= 32) {
		// split the block
		// first part becomes allocated
		set_header(header, adjusted_size, 1);
		ics_footer *footer = get_footer_from_header(header);
		set_footer(footer, adjusted_size, 0, 1); // requested_size will be set by caller

		// second part becomes a new free block
		ics_header *new_free_header = (ics_header *)((char *)header + adjusted_size);
		set_header(new_free_header, remainder, 0);

		ics_footer *new_free_footer = get_footer_from_header(new_free_header);
		set_footer(new_free_footer, remainder, 0, 0);

		// insert the new free block into appropriate bucket
		int new_bucket_idx = find_bucket_index(remainder);
		ics_free_header *new_free_block = (ics_free_header *)new_free_header;
		new_free_block->next = NULL;
		new_free_block->prev = NULL;
		insert_free_block(new_free_block, new_bucket_idx);
	} else {
		// dont split, use entire block to avoid splinters
		set_header(header, total_size, 1);
		ics_footer *footer = get_footer_from_header(header);
		set_footer(footer, total_size, 0, 1); // req size will be set by caller
	}
}

/* ============== PROVIDED FUNCTIONS =============== */

void *ics_malloc(size_t size) {
	// handle size 0
	if (size == 0) {
		errno = EINVAL;
		return NULL;
	}

	// init heap on first call
	if (!heap_initialized) {
		if (init_heap() == -1) {
			return NULL; // errno already set
		}
	}

	// calc the adjusted block size (including header, footer, alignment)
	size_t adjusted_size = calculate_block_size(size);

	// check if this would exceed max possible allocation
	// max: 6 pages = 24576 bytes, minus prologue (8) and epilogue (8) = 24560 bytes
	if (adjusted_size > (MAX_PAGES * PAGE_SIZE - 16)) {
		errno = ENOMEM;
		return NULL;
	}

	// search for a fit
	ics_free_header *fit = find_fit(adjusted_size);

	// if no fit found, extend heap
	while (fit == NULL) {
		void *new_block = extend_heap(adjusted_size);
		if (new_block == NULL) {
			// extend_heap func already set errno
			return NULL;
		}
		
		// try again
		fit = find_fit(adjusted_size);
	}

	// place the block and split if necessary
	place_block(fit, adjusted_size);

	// update the footer w/ req size
	ics_header *header = &(fit->header);
	ics_footer *footer = get_footer_from_header(header);
	footer->requested_size = size;

	// return pointer to payload
	return get_payload_from_header(header);
}


int ics_free(void *ptr) {
	// validate pointer
	if (!is_valid_free_pointer(ptr)) {
		errno = EINVAL;
		return -1;
	}

	// additional validation: check if ptr is w/in heap bounds
	if (ptr < heap_start || ptr >= heap_end) {
		errno = EINVAL;
		return -1;
	}

	// get header
	ics_header *header = get_header_from_payload(ptr);

	// check if header is w/in valid range
	if ((void *)header < heap_start || (void *)header >= heap_end) {
		errno = EINVAL;
		return -1;
	}

	// mark block as free
	uint64_t block_size = get_size_from_header(header);
	set_header(header, block_size, 0);

	ics_footer *footer = get_footer_from_header(header);
	footer->block_size = (footer->block_size & ~0x1); // clear alloc'd bit only
	
	// coalesce with adjacent free* blocks
	coalesce(header);

	return 0;
}
