import hashlib
import struct

def verify_log(log_file, claimed_hash):
    """
    Replays the binary log and checks if it matches the claimed hash.
    """
    hasher = hashlib.sha256()
    
    with open(log_file, "rb") as f:
        data = f.read()
        
    i = 0
    print("--- REPLAYING MISSION TRACE ---")
    
    while i < len(data):
        tag = data[i]
        i += 1
        
        if tag == 1: # Branch Decision (1 byte)
            val = data[i:i+1]
            print(f"[{i}] Branch: {'TRUE' if val == b'1' else 'FALSE'}")
            hasher.update(val)
            i += 1
            
        elif tag == 2: # Indirect Jump (8 bytes)
            addr = struct.unpack("<Q", data[i:i+8])[0]
            print(f"[{i}] Indirect Jump -> 0x{addr:x}")
            hasher.update(data[i:i+8])
            i += 8
            
        elif tag == 3: # Return / Stack Event (4 bytes)
            func_id = struct.unpack("<I", data[i:i+4])[0]
            print(f"[{i}] Function Return ID: {func_id}")
            hasher.update(data[i:i+4])
            i += 4

    calculated_hash = hasher.hexdigest()
    print(f"\nCalculated Hash: {calculated_hash}")
    print(f"Claimed Hash:    {claimed_hash}")
    
    if calculated_hash == claimed_hash:
        print("\n✅ SUCCESS: Mission Log is Authenticated!")
    else:
        print("\n❌ FAILURE: Hash Mismatch! Log has been tampered with.")

# Example Usage
# verify_log("mission.log", "282fde5a8ac4d05703a9028be03f9d762036eb79e91b88957519c6ee0cb10e4b")
