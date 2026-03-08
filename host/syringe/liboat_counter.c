#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Counters for dynamic instrumentation events */
static unsigned long count_branch = 0;
static unsigned long count_func_enter = 0;
static unsigned long count_func_exit = 0;
static unsigned long count_indirect = 0;
static unsigned long count_init = 0;

void __oat_init(void) {
    count_init++;
    /* Reset per-operation counters (paper measures per-operation) */
    count_branch = 0;
    count_func_enter = 0;
    count_func_exit = 0;
    count_indirect = 0;
}

void __oat_log(int val) {
    count_branch++;
}

void __oat_log_indirect(uint64_t target_addr) {
    count_indirect++;
}

void __oat_func_enter(int func_id) {
    count_func_enter++;
}

void __oat_func_exit(int func_id) {
    count_func_exit++;
}

void __oat_print_proof(void) {
    printf("\n=== OAT Dynamic Instrumentation Counts ===\n");
    printf("  __oat_init calls:          %lu\n", count_init);
    printf("  B.Cond  (branch logs):     %lu    (paper: 488)\n", count_branch);
    printf("  Ret     (func exits):      %lu    (paper: 1946)\n", count_func_exit);
    printf("  Icall   (indirect calls):  %lu    (paper: 1)\n", count_indirect);
    printf("  Func entries:              %lu\n", count_func_enter);
    printf("============================================\n\n");
}

void __oat_export_log(const char* filename) {
    /* no-op for counting mode */
}
