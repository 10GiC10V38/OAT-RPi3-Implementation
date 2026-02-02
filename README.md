---

## 🔒 Secure Logging & Verification System

This branch introduces the **Offline Verification Engine**. Unlike simple console logging, this system provides a cryptographically tamper-proof audit trail of the drone's flight.

### Architecture

1. **The Recorder (TrustZone):**

   * As the drone flies, the Trusted Application (TA) records every security event *(Branch taken, Function Pointer Target, Return ID)* into a secure **8KB buffer**.
   * Simultaneously, it updates a running **SHA-256 hash** *(The Proof)*.

2. **The Export (Normal World):**

   * When the mission ends, the host application calls `__oat_export_log()` to save the binary history to `mission.bin`.

3. **The Auditor (Python Verifier):**

   * An external Python script reads `mission.bin` and the **Final Hash**.
   * It replays the events mathematically to ensure the log matches the hash perfectly.

---

## ✅ How to Run the Verifier

### Step 1: Generate the Log

Run the drone application on the Raspberry Pi:

```bash
drone_app 1
# Output:
# [OAT] Mission Log saved to 'mission.bin' (145 bytes)
# [OAT] Final Execution Proof: 282fde5...
```

---

