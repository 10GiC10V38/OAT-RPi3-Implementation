#include <stdio.h>
#include <stdlib.h>

// Forward declaration of helper
void __oat_print_proof();

int main(int argc, char *argv[]) {
    printf("--- Drone Logic Starting ---\n");

    // Branch 1
    if (argc > 1) {
        printf("Mode: ACTIVE flight\n");
    } else {
        printf("Mode: IDLE standby\n");
    }

    // Branch 2
    int battery = 80;
    if (battery < 20) {
        printf("Warning: Low Battery!\n");
    } else {
        printf("Battery OK.\n");
    }

    printf("--- Drone Logic Finished ---\n");
    
    // Get the proof from TEE
    __oat_print_proof(); 
    return 0;
}
