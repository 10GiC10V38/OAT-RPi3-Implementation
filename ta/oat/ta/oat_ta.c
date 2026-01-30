/* oat_ta.c */
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <oat_ta.h>

#define MAX_STACK_DEPTH 128

/* Session Context: Holds the state for ONE running program */
typedef struct {
    uint32_t shadow_stack[MAX_STACK_DEPTH];
    int stack_ptr;
    TEE_OperationHandle op_handle; // For the running hash
    bool is_crypto_initialized;
} oat_session_ctx;

/* ------------------------------------------------------------------------
 * TA Entry Points (Boilerplate)
 * ------------------------------------------------------------------------ */

TEE_Result TA_CreateEntryPoint(void) {
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[4],
                                    void **sess_ctx) {
    (void)&param_types;
    (void)&params;

    // Allocate memory for this session's context
    oat_session_ctx *ctx = TEE_Malloc(sizeof(oat_session_ctx), 0);
    if (!ctx) return TEE_ERROR_OUT_OF_MEMORY;

    // Initialize state
    ctx->stack_ptr = 0;
    ctx->op_handle = TEE_HANDLE_NULL;
    ctx->is_crypto_initialized = false;

    *sess_ctx = (void *)ctx;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx) {
    oat_session_ctx *ctx = (oat_session_ctx *)sess_ctx;
    
    // Clean up Crypto Handle
    if (ctx->op_handle != TEE_HANDLE_NULL) {
        TEE_FreeOperation(ctx->op_handle);
    }
    
    // Free the session memory
    TEE_Free(ctx);
}

/* ------------------------------------------------------------------------
 * Helper Functions
 * ------------------------------------------------------------------------ */

static TEE_Result init_session(oat_session_ctx *ctx) {
    // Reset Stack
    ctx->stack_ptr = 0;

    // Setup SHA-256
    if (ctx->op_handle != TEE_HANDLE_NULL) {
        TEE_FreeOperation(ctx->op_handle);
    }
    
    // [FIX] Changed maxKeySize from 256 to 0. 
    // Digests do not use keys, so this must be 0.
    TEE_Result res = TEE_AllocateOperation(&ctx->op_handle, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
    
    if (res != TEE_SUCCESS) {
        EMSG("OAT: Failed to allocate SHA256 operation: 0x%x", res);
        return res;
    }

    // Start the Digest
    TEE_DigestUpdate(ctx->op_handle, NULL, 0); 
    
    ctx->is_crypto_initialized = true;
    return TEE_SUCCESS;
}

static void update_running_hash(oat_session_ctx *ctx, uint32_t val) {
    if (!ctx->is_crypto_initialized) return;
    
    // Mix the event ID into the hash
    TEE_DigestUpdate(ctx->op_handle, (void*)&val, sizeof(uint32_t));
}

/* ------------------------------------------------------------------------
 * Command Handler
 * ------------------------------------------------------------------------ */

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx,
                                      uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    
    oat_session_ctx *ctx = (oat_session_ctx *)sess_ctx;
    uint32_t val;

    switch (cmd_id) {
        
        // --- 1. INITIALIZE ---
        case CMD_HASH_INIT:
            return init_session(ctx);

        // --- 2. FORWARD EDGE (BRANCHES) ---
        case CMD_HASH_UPDATE:
            // Expects a small buffer (e.g., "1" or "0")
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            // [FIX] Add Safety Check: Don't use handle if init failed!
            if (!ctx->is_crypto_initialized) {
                 return TEE_ERROR_BAD_STATE;
            }

            // Add the branch decision to the running hash
            TEE_DigestUpdate(ctx->op_handle, 
                             params[0].memref.buffer, 
                             params[0].memref.size);
            return TEE_SUCCESS;

        // --- 3. FINALIZE (GET PROOF) ---
        case CMD_HASH_FINAL:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_OUTPUT)
                return TEE_ERROR_BAD_PARAMETERS;
            
            uint32_t out_size = params[0].memref.size;
            return TEE_DigestDoFinal(ctx->op_handle, NULL, 0, 
                                     params[0].memref.buffer, &out_size);

        // --- 4. BACKWARD EDGE (SHADOW STACK PUSH) ---
        case CMD_STACK_PUSH:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;

            val = params[0].value.a; // Function ID

            if (ctx->stack_ptr >= MAX_STACK_DEPTH) {
                EMSG("OAT ERROR: Shadow Stack Overflow!");
                return TEE_ERROR_OVERFLOW;
            }

            // Push to Stack
            ctx->shadow_stack[ctx->stack_ptr++] = val;
            
            // Also hash this "Call" event so it's part of the proof
            update_running_hash(ctx, val);
            
            return TEE_SUCCESS;

        // --- 5. BACKWARD EDGE (SHADOW STACK POP) ---
        case CMD_STACK_POP:
            if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INPUT)
                return TEE_ERROR_BAD_PARAMETERS;

            val = params[0].value.a; // Function ID attempting to return

            if (ctx->stack_ptr <= 0) {
                EMSG("OAT ERROR: Shadow Stack Underflow (Return without Call)!");
                return TEE_ERROR_SECURITY;
            }

            // Pop from Stack
            ctx->stack_ptr--;
            uint32_t expected_id = ctx->shadow_stack[ctx->stack_ptr];

            // *** THE SECURITY CHECK ***
            if (expected_id != val) {
                EMSG("#############################################");
                EMSG("# SECURITY ALERT: ROP ATTACK DETECTED!      #");
                EMSG("# Expected Return: Func ID %u               #", expected_id);
                EMSG("# Actual Return:   Func ID %u               #", val);
                EMSG("#############################################");
                
                // Fail the operation to block the return
                return TEE_ERROR_SECURITY; 
            }

            // If safe, hash this "Return" event
            update_running_hash(ctx, val);

            return TEE_SUCCESS;

        default:
            return TEE_ERROR_BAD_PARAMETERS;
    }
}
