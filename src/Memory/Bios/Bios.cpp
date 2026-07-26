#include "Bios.h"

#include <vector>
#include <iostream>

Bios::Bios(const std::string& path) : data(0), cache(0)  {
    reset(path);
}

void Bios::reset(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
    
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file");
	}
    
	std::vector<uint8_t> content;
    
	// Get the file size
	file.seekg(0, std::ios::end);
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
    
	if (size < 0) {
		throw std::runtime_error("Failed to determine file size");
		return;
	}
    
	// I'm actually speechless..
	// I was loading the bios in a wrong format.. ;-;
	content.resize(size);
	data.resize(size);
	cache.reserve(size / sizeof(uint32_t));
    
	if (!file.read(reinterpret_cast<char*>(content.data()), size)) {
		std::cerr << "no file content/\n";
		throw std::runtime_error("Failed to read file content");
	}
    
	std::copy(content.begin(), content.end(), data.begin());
    
	auto write = [&](uint32_t address, uint32_t opcode) {
		address &= data.size() - 1;
        
		for (int i = 0; i < 4; i++) {
			data[address + i] = (opcode >> (i * 8)) & 0xff;
		}
	};
    
	// Enable TTY
	//write(0x6F0C, 0x24010001);
	//write(0x6F14, 0xAF81A9C0);
    
	file.close();
    
	ptr = data.data();
    
	if (data.size() != BIOS_SIZE) {
		throw std::runtime_error("Invalid BIOS size");
	}
	
	std::cerr << "size is gud\n";
}
