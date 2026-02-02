/* host/liboat.c */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <tee_client_api.h>

/* --- CONFIGURATION --- */
#define TA_OAT_UUID \
    { 0x92b192d1, 0x9686, 0x424a, \
      { 0x8d, 0x18, 0x97, 0xc1, 0x18, 0x12, 0x95, 0x70} }

#define CMD_HASH_INIT     4
#define CMD_HASH_UPDATE   5
#define CMD_HASH_FINAL    6
#define CMD_STACK_PUSH    0x10
#define CMD_STACK_POP     0x11
#define CMD_INDIRECT_CALL 0x12
#define CMD_GET_LOG 0x13

/* Global Context */
static TEEC_Context ctx;
static TEEC_Session sess;
static int is_initialized = 0;

/* Initialize Session */
void __oat_init() {
    if (is_initialized) return;
    TEEC_Result res;
    TEEC_UUID uuid = TA_OAT_UUID;
    uint32_t err_origin;

    TEEC_InitializeContext(NULL, &ctx);
    TEEC_OpenSession(&ctx, &sess, &uuid, TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);
    TEEC_InvokeCommand(&sess, CMD_HASH_INIT, NULL, NULL);
    is_initialized = 1;
    printf("[OAT] Secure Session Established.\n");
}

/* 1. Branch Logging */
void __oat_log(int val) {
    if (!is_initialized) __oat_init();
    TEEC_Operation op = {0};
    char buffer[2]; 
    sprintf(buffer, "%d", val);
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = 1;
    TEEC_InvokeCommand(&sess, CMD_HASH_UPDATE, &op, NULL);
}

/* 2. Indirect Jump Logging (NEW) */
void __oat_log_indirect(uint64_t target_addr) {
    if (!is_initialized) __oat_init();
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    
    // Split 64-bit address into two 32-bit halves
    op.params[0].value.a = (uint32_t)(target_addr & 0xFFFFFFFF);
    op.params[0].value.b = (uint32_t)(target_addr >> 32);

    TEEC_InvokeCommand(&sess, CMD_INDIRECT_CALL, &op, NULL);
}

/* 3. Shadow Stack: Entry */
void __oat_func_enter(int func_id) {
    if (!is_initialized) __oat_init();
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = func_id;
    TEEC_InvokeCommand(&sess, CMD_STACK_PUSH, &op, NULL);
}

/* 4. Shadow Stack: Exit */
void __oat_func_exit(int func_id) {
    if (!is_initialized) return;
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = func_id;
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_STACK_POP, &op, NULL);
    
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "\n[OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.\n");
        exit(1); 
    }
}

void __oat_get_execution_log(uint8_t *buffer, uint32_t *size) {
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = *size;
    
    // Call TA to get the blob
    TEEC_InvokeCommand(&sess, CMD_GET_LOG, &op, NULL);
    *size = op.params[0].tmpref.size;
}


// Function to retrieve the log blob
void __oat_export_log(const char* filename) {
    if (!is_initialized) return;

    // Buffer to hold the log (Match TA size)
    uint8_t buffer[8192];
    TEEC_Operation op = {0};
    
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = buffer;
    op.params[0].tmpref.size = sizeof(buffer);

    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_GET_LOG, &op, NULL);
    
    if (res != TEEC_SUCCESS) {
        printf("[OAT] Failed to export log: 0x%x\n", res);
        return;
    }

    // Write to file
    uint32_t actual_size = op.params[0].tmpref.size;
    FILE *f = fopen(filename, "wb");
    if (f) {
        fwrite(buffer, 1, actual_size, f);
        fclose(f);
        printf("[OAT] Mission Log saved to '%s' (%u bytes)\n", filename, actual_size);
    } else {
        printf("[OAT] Error opening file for writing.\n");
    }
}

/* Helper to Print Proof */
void __oat_print_proof() {
    uint8_t hash[32];
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = hash;
    op.params[0].tmpref.size = 32;
    TEEC_InvokeCommand(&sess, CMD_HASH_FINAL, &op, NULL);
    printf("[OAT] Final Execution Proof: ");
    for(int i=0; i<32; i++) printf("%02x", hash[i]);
    printf("\n");
}
