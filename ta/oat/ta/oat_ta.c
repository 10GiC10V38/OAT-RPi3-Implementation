/* ta/oat/ta/oat_ta.c */
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <oat_ta.h>

#define MAX_STACK_DEPTH 128
#define MAX_LOG_SIZE    8192  // 8KB Log Buffer

typedef struct {
    uint32_t shadow_stack[MAX_STACK_DEPTH];
    int stack_ptr;
    TEE_OperationHandle op_handle;
    bool is_crypto_initialized;
    
    // NEW: Log Storage
    uint8_t execution_log[MAX_LOG_SIZE];
    uint32_t log_idx;
} oat_session_ctx;

/* Entry Points (Boilerplate) */
TEE_Result TA_CreateEntryPoint(void) { return TEE_SUCCESS; }
void TA_DestroyEntryPoint(void) { }

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types, TEE_Param params[4], void **sess_ctx) {
    (void)&param_types; (void)&params;
    oat_session_ctx *ctx = TEE_Malloc(sizeof(oat_session_ctx), 0);
    if (!ctx) return TEE_ERROR_OUT_OF_MEMORY;
    
    ctx->stack_ptr = 0;
    ctx->log_idx = 0;
    ctx->op_handle = TEE_HANDLE_NULL;
    ctx->is_crypto_initialized = false;
    *sess_ctx = (void *)ctx;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx) {
    oat_session_ctx *ctx = (oat_session_ctx *)sess_ctx;
    if (ctx->op_handle != TEE_HANDLE_NULL) TEE_FreeOperation(ctx->op_handle);
    TEE_Free(ctx);
}

/* --- Helpers --- */

static TEE_Result init_session(oat_session_ctx *ctx) {
    ctx->stack_ptr = 0;
    ctx->log_idx = 0; // Reset Log
    
    if (ctx->op_handle != TEE_HANDLE_NULL) TEE_FreeOperation(ctx->op_handle);
    TEE_Result res = TEE_AllocateOperation(&ctx->op_handle, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
    if (res != TEE_SUCCESS) return res;

    TEE_DigestUpdate(ctx->op_handle, NULL, 0); 
    ctx->is_crypto_initialized = true;
    return TEE_SUCCESS;
}

static void update_running_hash(oat_session_ctx *ctx, void* data, size_t size) {
    if (!ctx->is_crypto_initialized) return;
    TEE_DigestUpdate(ctx->op_handle, data, size);
}

// NEW: Append to internal log buffer
static void append_log(oat_session_ctx *ctx, uint8_t tag, void* data, uint32_t size) {
    // Check for overflow (1 byte tag + payload size)
    if (ctx->log_idx + 1 + size > MAX_LOG_SIZE) {
        EMSG("OAT Log Overflow! Dropping event.");
        return; 
    }
    
    // Write Tag
    ctx->execution_log[ctx->log_idx++] = tag;
    
    // Write Data
    if (size > 0 && data != NULL) {
        TEE_MemMove(&ctx->execution_log[ctx->log_idx], data, size);
        ctx->log_idx += size;
    }
}

/* --- Command Handler --- */

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx, uint32_t cmd_id,
                                      uint32_t param_types, TEE_Param params[4]) {
    oat_session_ctx *ctx = (oat_session_ctx *)sess_ctx;
    uint32_t val;
    uint64_t addr_target;
    char *decision_char;

    switch (cmd_id) {
        case CMD_HASH_INIT:
            return init_session(ctx);

        // 1. BRANCH LOGGING
        case CMD_HASH_UPDATE: 
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            if (!ctx->is_crypto_initialized) return TEE_ERROR_BAD_STATE;
            
            // Hash it
            TEE_DigestUpdate(ctx->op_handle, params[0].memref.buffer, params[0].memref.size);
            
            // Log it
            append_log(ctx, TAG_BRANCH, params[0].memref.buffer, params[0].memref.size);
            return TEE_SUCCESS;

        case CMD_HASH_FINAL:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            uint32_t out_size = params[0].memref.size;
            return TEE_DigestDoFinal(ctx->op_handle, NULL, 0, params[0].memref.buffer, &out_size);

        // 2. SHADOW STACK PUSH (Not logged to file, only tracked in RAM)
        case CMD_STACK_PUSH:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT) return TEE_ERROR_BAD_PARAMETERS;
            val = params[0].value.a;
            if (ctx->stack_ptr >= MAX_STACK_DEPTH) return TEE_ERROR_OVERFLOW;
            
            ctx->shadow_stack[ctx->stack_ptr++] = val;
            update_running_hash(ctx, &val, sizeof(uint32_t));
            return TEE_SUCCESS;

        // 3. SHADOW STACK POP (Logged!)
        case CMD_STACK_POP:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT) return TEE_ERROR_BAD_PARAMETERS;
            val = params[0].value.a;
            if (ctx->stack_ptr <= 0) return TEE_ERROR_SECURITY;
            
            ctx->stack_ptr--;
            uint32_t expected = ctx->shadow_stack[ctx->stack_ptr];
            if (expected != val) {
                EMSG("SECURITY ALERT: ROP ATTACK! Exp: %u, Got: %u", expected, val);
                return TEE_ERROR_SECURITY;
            }
            update_running_hash(ctx, &val, sizeof(uint32_t));
            
            // Log the return event
            append_log(ctx, TAG_STACK_POP, &val, sizeof(uint32_t));
            return TEE_SUCCESS;

        // 4. INDIRECT JUMP (Logged!)
        case CMD_INDIRECT_CALL:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;

            addr_target = params[0].value.a;
            addr_target |= ((uint64_t)params[0].value.b << 32);

            update_running_hash(ctx, &addr_target, sizeof(uint64_t));
            
            // Log the target address
            append_log(ctx, TAG_INDIRECT, &addr_target, sizeof(uint64_t));
            return TEE_SUCCESS;

        // 5. GET LOG (Export to Host)
        case CMD_GET_LOG:
             if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
                return TEE_ERROR_BAD_PARAMETERS;
             
             uint32_t req_size = params[0].memref.size;
             if (req_size < ctx->log_idx) {
                 params[0].memref.size = ctx->log_idx; // Tell Host needed size
                 return TEE_ERROR_SHORT_BUFFER;
             }
             
             // Copy buffer to Host
             TEE_MemMove(params[0].memref.buffer, ctx->execution_log, ctx->log_idx);
             params[0].memref.size = ctx->log_idx; // Return actual size
             return TEE_SUCCESS;

        default:
            return TEE_ERROR_BAD_PARAMETERS;
    }
}
