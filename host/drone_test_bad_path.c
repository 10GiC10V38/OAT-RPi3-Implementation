#include <stdio.h>
#include <stdlib.h>

// Forward declarations
void __oat_print_proof();
void __oat_func_enter(int id);
void __oat_func_exit(int id);

// A helper function to simulate an attack
void attempt_hack() {
    printf("\n[!!!] SIMULATING ROP ATTACK [!!!]\n");
    printf("Attacker tries to force a return with WRONG ID...\n");
    
    // ATTACK: We manually trigger an exit with a random ID (e.g., 9999)
    // The Trusted App expects the ID of 'attempt_hack' (which is NOT 9999)
    __oat_func_exit(9999); 
}

int main(int argc, char *argv[]) {
    printf("--- Drone Logic Starting ---\n");

    // Normal Logic
    if (argc > 1) {
        printf("Mode: ACTIVE flight\n");
    } else {
        printf("Mode: IDLE standby\n");
    }

    // Trigger the attack simulation
    attempt_hack();

    printf("--- Drone Logic Finished ---\n");
    __oat_print_proof(); 
    return 0;
}
