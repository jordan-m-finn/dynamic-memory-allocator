#include "debug.h"
#include "helpers4.h"
#include "icsmm.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>

#define VALUE1_VALUE 320
#define VALUE2_VALUE 0xDEADBEEFF00D

void press_to_cont() {
    printf("Press Enter to Continue");
    while (getchar() != '\n')
      ;
    printf("\n");
}

void null_check(void* ptr, long size) {
    if (ptr == NULL) {
      error(
          "Failed to allocate %lu byte(s) for an integer using ics_malloc.\n",
          size);
      error("%s\n", "Aborting...");
      assert(false);
    } else {
      success("ics_malloc returned a non-null address: %p\n", (void *)(ptr));
    }
}

void payload_check(void* ptr) {
    if ((unsigned long)(ptr) % 16 != 0) {
      warn("Returned payload address is not divisble by a quadword. %p %% 16 "
           "= %lu\n",
           (void *)(ptr), (unsigned long)(ptr) % 16);
    }
}

int main(int argc, char *argv[]) {
  // Initialize the custom allocator
  ics_mem_init(NULL);

  // Tell the user about the fields
  info("Initialized heap\n");
  press_to_cont();

  // Print out title for first test
  printf("=== Test1: Allocation test ===\n");
  // Test #1: Allocate an integer
  int *value1 = ics_malloc(sizeof(int));
  null_check(value1, sizeof(int));
  payload_check(value1);
  ics_payload_print((void*)value1);
  press_to_cont();

  // Now assign a value
  printf("=== Test2: Assignment test ===\n");
  info("Attempting to assign value1 = %d\n", VALUE1_VALUE);
  // Assign the value
  *value1 = VALUE1_VALUE;
  // Now check its value
  CHECK_PRIM_CONTENTS(value1, VALUE1_VALUE, "%d", "value1");
  ics_payload_print((void*)value1);
  press_to_cont();

  printf("=== Test3: Allocate a second variable ===\n");
  info("Attempting to assign value2 = %ld\n", VALUE2_VALUE);
  long *value2 = ics_malloc(sizeof(long));
  null_check(value2, sizeof(long));
  payload_check(value2);
  // Assign a value
  *value2 = VALUE2_VALUE;
  // Check value
  CHECK_PRIM_CONTENTS(value2, VALUE2_VALUE, "%ld", "value2");
  ics_payload_print((void*)value2);
  press_to_cont();

  printf("=== Test4: does value1 still equal %d ===\n", VALUE1_VALUE);
  CHECK_PRIM_CONTENTS(value1, VALUE1_VALUE, "%d", "value1");
  ics_payload_print((void*)value1);
  press_to_cont();

  // Free a variable
  printf("=== Test5: Free a block and snapshot ===\n");
  info("%s\n", "Freeing value1...");
  ics_free(value1);
  ics_buckets_print();
  press_to_cont();

  // Free a variable
  printf("=== Test6: Do blocks go in correct bucket? ===\n");
  void *value3 = ics_malloc(128);
  void *value4 = ics_malloc(128);
  void *value5 = ics_malloc(128);
  void *value6 = ics_malloc(128);
  ics_free(value3);
  ics_free(value5);
  ics_buckets_print();
  press_to_cont();
  ics_free(value4);
  ics_free(value6);

  // Allocate a large chunk of memory and then free it
  printf("=== Test7: 8192 byte allocation ===\n");
  void *memory = ics_malloc(8192);
  ics_buckets_print();
  press_to_cont();
  ics_free(memory);
  ics_buckets_print();
  press_to_cont();

  ics_mem_fini();

  return EXIT_SUCCESS;
}
