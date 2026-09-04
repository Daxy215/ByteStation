#pragma once

#include <cstdint>
#include <vector>

class MemoryCard {
public:
	enum Mode {
		Idle,
		Connected,
		Read,
		Write,
		GetId,
	};
	
	// TODO;   00h  4xh   Receive Memory End Byte (47h=Good, 4Eh=BadChecksum, FFh=BadSector)
	enum Stats : uint8_t { Good = 0x47, BadChecksum = 0x4E, BadSector = 0xFF };
	
	union Flag {
		struct {
			uint8_t         : 2; // Docs don't mention anything about those? There is only about bit 2, bit 3, and 5(which is always 1)
			uint8_t error   : 1; // Used to indicate errors
			uint8_t fresh   : 1; 
			uint8_t unknown : 1; // Note: Some (not all) non-sony cards also have Bit5 of the FLAG byte set.
			uint8_t         : 4;
		};
		
		uint8_t _reg;
		
		/**
		 * Docs says on power-up bit3 should be 1,
		 * to indicate that the directory? wasn't read yet
		 */
		Flag() : error(0), fresh(1), unknown(1) {}
	};
	
public:
	MemoryCard();
	
	uint16_t handle(uint32_t val);
	
private:
	uint8_t handleRead(uint8_t val);
	uint8_t handleWrite(uint8_t val);
	
public:
	void reset();

private:
	void save() const;
	void load();
	
private:
	uint8_t state = 0;
	uint8_t checksum = 0;
	uint16_t sendAddress = 0;
	
public:
	bool _interrupt = false;
	
	Mode _mode = Idle;
	
private:
	Stats _stats;
	Flag _flag;
	
private:
	/**
	 * Data Size
     * Total Memory 128KB = 131072 bytes = 20000h bytes
     * 1 Block 8KB = 8192 bytes = 2000h bytes
     * 1 Frame 128 bytes = 80h bytes
	 */
	std::vector<uint8_t> data = { 0 };
	
};
