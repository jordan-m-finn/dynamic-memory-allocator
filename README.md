# Dynamic Memory Allocator

A custom implementation of `malloc` and `free` in C, featuring segregated free lists, immediate coalescing, and efficient memory management strategies.

## Overview

This project implements a dynamic memory allocator that manages heap memory allocation and deallocation. The allocator uses advanced techniques including segregated storage, boundary tags for constant-time coalescing, and first-fit placement policies to efficiently manage memory while minimizing fragmentation.

## Features

- **Segregated Free Lists**: Five size-class buckets (32, 64, 128, 256, ∞ bytes) for fast allocation
- **First-Fit Placement**: Searches appropriate size class and larger classes until suitable block is found
- **LIFO Insertion**: Newly freed blocks inserted at head of free list for O(1) insertion
- **Immediate Bi-Directional Coalescing**: Merges adjacent free blocks on every free operation
- **Boundary Tags**: Footers enable constant-time backward traversal for coalescing
- **16-Byte Alignment**: All payloads aligned to 16-byte boundaries for optimal performance
- **Splinter Prevention**: Blocks only split if remainder ≥ 32 bytes minimum block size
- **Magic Number Validation**: Headers and footers tagged for integrity checking

## Architecture

### Memory Block Structure

Each memory block consists of:
```
┌─────────────────────────────────────┐
│ Header (8 bytes)                    │
│  - block_size (16 bits) with alloc  │
│  - hid magic: 0x100DECAFBEE5        │
├─────────────────────────────────────┤
│ Payload (minimum 16 bytes)          │
│  - User data when allocated         │
│  - next/prev pointers when free     │
├─────────────────────────────────────┤
│ Footer (8 bytes)                    │
│  - block_size (16 bits) with alloc  │
│  - requested_size (16 bits)         │
│  - fid magic: 0x0A011DAB            │
└─────────────────────────────────────┘
```

### Heap Layout
```
┌────────────────────────────────────┐
│ Prologue (8-byte footer, alloc)   │ ← Always allocated, size 0
├────────────────────────────────────┤
│                                    │
│   User Blocks (alloc/free mix)    │
│                                    │
├────────────────────────────────────┤
│ Epilogue (8-byte header, alloc)   │ ← Always allocated, size 0
└────────────────────────────────────┘
```

### Segregated Free List Buckets

| Bucket | Size Range    | Purpose                           |
|--------|---------------|-----------------------------------|
| 0      | (0, 32]       | Small allocations                 |
| 1      | (32, 64]      | Medium-small allocations          |
| 2      | (64, 128]     | Medium allocations                |
| 3      | (128, 256]    | Medium-large allocations          |
| 4      | (256, ∞]      | Large allocations                 |

## Implementation Details

### Block Size Calculation

1. Minimum payload: 16 bytes (for next/prev pointers in free blocks)
2. Total size: Header (8) + Payload + Footer (8)
3. Round up to 16-byte alignment
4. Minimum block size: 32 bytes

**Example**: `malloc(20)` → 48 bytes (8 + 32 + 8 = 48, already aligned)

### Coalescing (4 Cases)
```c
Case 1: [allocated] [current] [allocated]  → No coalescing
Case 2: [allocated] [current] [free]       → Merge with next
Case 3: [free] [current] [allocated]       → Merge with previous  
Case 4: [free] [current] [free]            → Merge all three blocks
```

### Allocation Algorithm

1. Calculate adjusted block size (alignment + minimum size)
2. Find appropriate bucket based on size
3. First-fit search within bucket
4. If no fit, search next larger bucket
5. If still no fit, extend heap by requesting new pages
6. Split block if remainder ≥ 32 bytes
7. Return pointer to payload (16-byte aligned)

### Free Algorithm

1. Validate pointer (5 checks):
   - Pointer within heap bounds
   - Header magic number correct
   - Footer magic number correct  
   - Block sizes match in header/footer
   - Allocated bit set in both header/footer
2. Mark block as free
3. Check adjacent blocks
4. Coalesce with any adjacent free blocks
5. Insert into appropriate bucket (LIFO)

## Building and Testing

### Compilation
```bash
make all
```

This compiles:
- `helpers4.c` - Helper functions for block manipulation
- `icsmm.c` - Main allocator implementation
- Test programs

### Running Tests
```bash
./bin/test1.bin
```

### Project Structure
```
.
├── include/
│   ├── icsmm.h          # Main allocator interface
│   ├── helpers4.h       # Helper function declarations
│   └── debug.h          # Debug macros
├── src/
│   ├── icsmm.c          # Allocator implementation
│   └── helpers4.c       # Helper function definitions
├── tests/
│   └── test1.c          # Test suite
└── Makefile
```

## API

### Core Functions
```c
void *ics_malloc(size_t size);
```
Allocates `size` bytes of memory. Returns pointer to 16-byte aligned payload, or NULL on failure.
```c
int ics_free(void *ptr);
```
Frees previously allocated block. Returns 0 on success, -1 on invalid pointer.

### Initialization Functions
```c
void ics_mem_init(size_t bounds[]);
```
Initializes the allocator and segregated buckets. Must be called before any allocation.
```c
void *ics_inc_brk();
```
Requests additional page (4096 bytes) from system. Returns pointer to new memory or -1 on failure.
```c
void *ics_get_brk();
```
Returns current position of the heap break pointer.

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| malloc    | O(f)           | f = free blocks in searched buckets |
| free      | O(1)           | Constant time with immediate coalescing |
| Coalesce  | O(1)           | Constant time with boundary tags |

## Memory Constraints

- Maximum heap size: 6 pages (24,576 bytes)
- Page size: 4,096 bytes
- Minimum block size: 32 bytes
- Alignment: 16 bytes

## Key Concepts Demonstrated

- **Explicit Memory Management**: Manual control over heap allocation
- **Data Structures in Payload**: Free list pointers stored in unused payload space
- **Bit Manipulation**: Allocated flag stored in LSB of block_size field
- **Pointer Arithmetic**: Navigation between headers, payloads, and footers
- **Fragmentation Mitigation**: Coalescing and splitting strategies
- **Boundary Tags**: Knuth's technique for bidirectional coalescing

## Technical Highlights

### Efficient Page Allocation

The allocator minimizes page requests by checking for existing free space at the end of the heap before extending:
```c
// Only request pages for the deficit
deficit = requested_size - existing_free_space_at_end;
pages_needed = ceil(deficit / PAGE_SIZE);
```

### Magic Number Validation

Each block includes magic numbers for integrity checking:
- Header ID (hid): `0x100DECAFBEE5`
- Footer ID (fid): `0x0A011DAB`

### Splinter Prevention

Blocks are only split if the remainder would be useful (≥ 32 bytes), preventing tiny unusable fragments.

## References

Based on concepts from *Computer Systems: A Programmer's Perspective* by Bryant & O'Hallaron:
- Chapter 9.9: Dynamic Memory Allocation
- Chapter 9.9.11: Boundary Tags (Knuth)
- Chapter 9.9.14: Segregated Free Lists

## License

MIT License - Feel free to use for educational purposes.
