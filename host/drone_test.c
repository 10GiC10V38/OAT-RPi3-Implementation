/* host/drone_test.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OAT Forward Declarations */
void __oat_print_proof();
void __oat_export_log(const char* filename);

/* --- Simulated Drone Drivers --- */
typedef struct {
    void (*init)();
    void (*shutdown)();
} DroneDriver;

void real_motor_init() {
    printf("[Motor] Spinning up rotors...\n");
}

void real_motor_stop() {
    printf("[Motor] Stopping rotors...\n");
}

void simulation_init() {
    printf("[Sim] Virtual drone ready.\n");
}

void simulation_stop() {
    printf("[Sim] Virtual drone paused.\n");
}

/* --- Main Logic --- */
int main(int argc, char *argv[]) {
    printf("--- Drone Logic Starting ---\n");

    DroneDriver driver;

    // 1. INDIRECT JUMP DECISION
    // The compiler doesn't know which function 'driver.init' points to yet.
    // OAT must track this assignment and the subsequent call.
    if (argc > 1) {
        printf("Mode: ACTIVE flight\n");
        driver.init = &real_motor_init;
        driver.shutdown = &real_motor_stop;
    } else {
        printf("Mode: IDLE standby\n");
        driver.init = &simulation_init;
        driver.shutdown = &simulation_stop;
    }

    // 2. TRIGGER INDIRECT CALL LOGGING
    // This looks like "driver.init()", but OAT will inject:
    // "__oat_log_indirect( address_of_function );"
    driver.init(); 

    printf("Battery OK.\n");

    // 3. ANOTHER INDIRECT CALL
    driver.shutdown();

    printf("--- Drone Logic Finished ---\n");
    
    // Print the final hash (Proof of Control Flow)
    
    // 1. Print Hash (The Proof)
    __oat_print_proof(); 
    
    // 2. Save Log (The Claim)
    __oat_export_log("mission.bin");
    
    return 0;
}
