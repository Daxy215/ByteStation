#include "Sector.h"

void Sector::set(const std::vector<uint8_t>& data) {
	_pointer = 0;
	_size = data.size();
	_buffer.resize(_size);
	
	std::memcpy(_buffer.data(), data.data(), _size);
}

uint8_t Sector::loadAt(uint32_t pos) {
	return _buffer[pos];
}

uint8_t& Sector::load8() {
	return _buffer[_pointer++];
}

int16_t& Sector::load16() {
	int16_t* data = reinterpret_cast<int16_t*>(&_buffer[_pointer]);
	_pointer += 2;
	
	return *data;
}

std::vector<uint8_t> Sector::read() {
	std::vector<uint8_t> data(_size);
	
	//std::memcpy(data.data(), &_buffer[_pointer], _size);
    std::memcpy(data.data(), _buffer.data(), _size);
	_pointer += _size;
	
	return data;
}

std::vector<uint8_t> Sector::read(size_t size) {
	std::vector<uint8_t> data(size);
	
	std::memcpy(data.data(), &_buffer[_pointer], size * 4);
	_pointer += size * 4;
	
	return data;
}

void const* Sector::data() const {
	return _buffer.data();
}

bool Sector::isEmpty() {
    return _pointer >= _size || _size == 0;
}

void Sector::empty() {
	// TODO; Delete data?
	
	_pointer = 0;
	_size = 0;
}
