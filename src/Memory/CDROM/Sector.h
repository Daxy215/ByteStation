#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

class Sector {
public:
	Sector() = default;
	
	Sector(size_t size) : _pointer(0), _size(0) {
		/**
		 * Sector contains;
		 * 12 bytes as a sync filed; Used for syncing to identify the beginning of the sector
		 * 4 bytes as the header; Contains the time to help locate data
		 * 8 bytes as the subheader; Used for error corection and data validation
		 * 2048 bytes as user data; This is accessable by the software, holds the executable data, assets or music
		 * 280 bytes used for error correction (ECC); Helps to correct minor read errors
		 * 4 bytes as the end of sector; Marks the end of the sector.
		 * 
		 * Sector size 2352 bytes,
		 * with 2048 being user data,
		 * and 304 used for ECC or other.
		 */
		
		//_buffer.reserve(size);
	}
	
	void set(const std::vector<uint8_t>& data);
	
	uint8_t loadAt(uint32_t pos);
	uint8_t& load8();
	int16_t& load16();
	
	std::vector<uint8_t> read();
	std::vector<uint8_t> read(size_t size);
	void const* data() const;
	
	bool isEmpty();
	void empty();

public:
	size_t _size;
	
public:
	size_t _pointer;
	
private:
	std::vector<uint8_t> _buffer;
	
public:
	static const int RAW_BUFFER = 2352;
	static const int XA_BUFFER = RAW_BUFFER * 16;
	
};
