#include <stdio.h>
#include <stdlib.h>
#include <tee_client_api.h>

/* --- CONFIGURATION --- */
// The New UUID matching the new TA
#define TA_OAT_UUID \
    { 0x92b192d1, 0x9686, 0x424a, \
      { 0x8d, 0x18, 0x97, 0xc1, 0x18, 0x12, 0x95, 0x70} }

// Command IDs (Must match oat_ta.c)
#define CMD_HASH_INIT    4
#define CMD_HASH_UPDATE  5
#define CMD_HASH_FINAL   6
#define CMD_STACK_PUSH   0x10
#define CMD_STACK_POP    0x11


/* Global Context */
static TEEC_Context ctx;
static TEEC_Session sess;
static int is_initialized = 0;

/* Initialize */
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

/* Branch Logging (Forward Edge) */
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

/* Function Entry (Shadow Stack PUSH) */
void __oat_func_enter(int func_id) {
    if (!is_initialized) __oat_init();
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = func_id;
    TEEC_InvokeCommand(&sess, CMD_STACK_PUSH, &op, NULL);
}

/* Function Exit (Shadow Stack POP) */
void __oat_func_exit(int func_id) {
    if (!is_initialized) return;
    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = func_id;
    TEEC_Result res = TEEC_InvokeCommand(&sess, CMD_STACK_POP, &op, NULL);
    
    // Panic if TEE says NO!
    if (res != TEEC_SUCCESS) {
        fprintf(stderr, "\n[OAT-FATAL] ROP ATTACK DETECTED! TEE blocked return.\n");
        exit(1); // Kill the process immediately
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
