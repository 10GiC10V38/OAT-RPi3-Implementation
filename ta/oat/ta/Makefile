CFG_TEE_TA_LOG_LEVEL ?= 4

# The UUID for the Trusted Application
BINARY = 92b192d1-9686-424a-8d18-97c118129570

# Check if TA_DEV_KIT_DIR is set
ifndef TA_DEV_KIT_DIR
$(error TA_DEV_KIT_DIR is not set. Run 'export TA_DEV_KIT_DIR=/path/to/optee/export-user_ta')
endif

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

.PHONY: clean
clean:
	rm -f *.o *.ta *.elf *.d *.map
	rm -f $(BINARY).ta
	rm -f $(BINARY).elf
