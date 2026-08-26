#include "MemoryCard.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../../../Utils/FileSystem/FileManager.h"

MemoryCard::MemoryCard() : data(128 * 1024) {
	load();
}

uint16_t MemoryCard::handle(uint32_t val) {
	switch(_mode) {
		case Idle: {
			if(val == 0x81) {
				_mode = Connected;
				_interrupt = true;
				
				return 0xFF;
			} else {
				_interrupt = false;
				
				return 0xFF;
			}
			
			break;
		}
		
		case Connected: {
			/**
			 * According to the docs, there are 3 types of commands:
			 * 0x52 Send Read Command  (ASCII "R"), Recieve FLAG Byte
			 * 0x57 Send Write Command (ASCII "W"), Receive FLAG Byte
			 * 0x53 Send Get ID Command (ASCII "S"), Receive FLAG Byte
			 */
			
			_interrupt = true;
			
			if(val == 0x52) {
				_mode = Read;
			} else if(val == 0x57) {
				_mode = Write;
			} else if(val == 0x53) {
				_mode = GetId;
			} else {
				_interrupt = false;
				return 0;
				//assert(false);
			}
			
			state = 0;
			
			auto ret = _flag._reg;
			_flag.error = 0;
			
			return ret;
		}
		
		case Read: {
			return handleRead(val & 0xFF);
			
			break;
		}
		
		case Write: {
			return handleWrite(val & 0xFF);
		}
		
		case GetId: {
			// This doesn't seem to be used anywhere?
			throw std::runtime_error("Error; Unimplemented 'GetId' for MemoryCard");
			
			break;
		}
	}
	
	return 0xFF;
}

uint8_t MemoryCard::handleRead(uint8_t val) {
	_interrupt = true;
	
	if(state >= 8 && state < 8 + 128) {
		auto n = state - 8;
	    
		// Receive Data Sector (128 bytes)
		uint8_t d = data[(sendAddress * 128) + n];
		checksum ^= d;
		state++;
		
		return d;
	}
	
	state++;
	
	switch(state - 1) {
		case 0: {
			return 0x5A; // Receive Memory Card ID1
		}
		
		case 1: {
			return 0x5D; // Receive Memory Card ID2
		}
		
		case 2: {
			// Send Address MSB  ;\sector number (0..3FFh)
			sendAddress = static_cast<uint16_t>(val << 8);
			
			return 0;
		}
		
		case 3: {
			// Send Address LSB  ;/
			sendAddress |= static_cast<uint16_t>(val);
			
			// 0 .. 3FF
			if(sendAddress >= 0x3FF)
				sendAddress &= 0x3FF;
			
			return 0;
		}
		
		case 4: {
			return 0x5C; // Receive Command Acknowledge 1  ;<-- late /ACK after this byte-pair
		}
		
		case 5: {
			return 0x5D; // Receive Command Acknowledge 2
		}
		
		case 6: {
			// Receive Confirmed Address MSB
			uint8_t msb = static_cast<uint8_t>(sendAddress >> 8);
			checksum = msb;
			
			return msb;
		}
		
		case 7: {
			// Receive Confirmed Address LSB
			uint8_t lsb = static_cast<uint8_t>(sendAddress & 0x00FF);
			checksum ^= lsb;
			
			return lsb;
		}
		
		case 136: {
			// Receive Checksum (MSB xor LSB xor Data bytes)
			
			return checksum;
		}
		
		case 137: {
			// Receive Memory End Byte (should be always 47h="G"=Good for Read)
			reset();
			
			return 'G';
		}
	}
}

uint8_t MemoryCard::handleWrite(uint8_t val) {
	_interrupt = true;
	
	if(state >= 4 && state < 4 + 128) {
		auto n = state - 4;
		
		// Send Data Sector (128 bytes)
		data[(sendAddress * 128) + n] = val;
		checksum ^= val;
		state++;
		return 0;
	}
	
	state++;
	
	switch(state - 1) {
		case 0: {
			return 0x5A; // Receive Memory Card ID1
		}
		
		case 1: {
			return 0x5D; // Receive Memory Card ID2
		}
		
		case 2: {
			// Send Address MSB  ;\sector number (0..3FFh)
			sendAddress = static_cast<uint16_t>(val << 8);
			checksum = static_cast<uint8_t>(sendAddress >> 8);
			
			return 0;
		}
		
		case 3: {
			// Send Address LSB  ;/
			sendAddress |= static_cast<uint16_t>(val);
			checksum ^= static_cast<uint8_t>(sendAddress & 0x00FF);
			
			_stats = Good;
			
			// 0 .. 3FF
			if(sendAddress >= 0x3FF) {
				sendAddress &= 0x3FF;
				_flag.error = 1;
				_stats = BadSector;
			}
			
			return 0;
		}
		
		case 132: {
			// Send Checksum (MSB xor LSB xor Data bytes)
			
			if(checksum != val) {
				_flag.error = 1;
				_stats = BadChecksum;
			}
			
			return 0;
		}
		
		case 133: {
			return 0x5C; // Receive Command Acknowledge 1
		}
		
		case 134: {
			return 0x5D; // Receive Command Acknowledge 2
		}
		
		case 135: {
			save();
			_flag.fresh = 0;
			
			reset();
			
			return _stats;
		}
		default: throw std::runtime_error("Error: Unhandled state in 'handleWrite' for the 'MemoryCard'!");
	}
}

void MemoryCard::reset() {
	_mode = Idle;
	_interrupt = false;
	state = 0;
}

void MemoryCard::save() {
	const std::string path = "MemoryCard/MemSave01.bin";
	
	std::filesystem::path resolvedPath = Emulator::Utils::FileManager::resolvePath(path);
	
	std::ofstream stream(resolvedPath, std::ios::binary);
	
	if(!stream) {
		std::cerr << "Creating a new save file at: " << path << "\n";
		
		std::filesystem::create_directories(resolvedPath.parent_path());
		
		return;
	}
	
	stream.write(reinterpret_cast<const char*>(data.data()), data.size());
	stream.close();
	
	std::cerr << "Saving to " << path << " was successful(I think)\n";
}

void MemoryCard::load() {
	const std::string path = "MemoryCard/MemSave01.bin";
	
	std::ifstream stream(Emulator::Utils::FileManager::resolvePath(path), std::ios::binary);
	
	if(!stream) {
		std::cerr << "Error couldn't find save file to load! " << path << "\n";
		return;
	}
	
	stream.read(reinterpret_cast<char*>(data.data()), data.size());
	stream.close();
	
	std::cerr << "Loading from" << path << " was successful(I think)\n";
}
