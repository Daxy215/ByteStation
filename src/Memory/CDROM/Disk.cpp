#include "Disk.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <cstdio>
#include <fstream>

#include "CDROM.h"
#include "../../Utils/FileSystem/FileManager.h"

Disk::Disk() = default;

/*std::vector<uint8_t> Disk::read(Location location) {
	auto buffer = std::vector<uint8_t>(Sector::RAW_BUFFER, 0);
	
	int pos = getTrackPosition(location);
	
	if(pos == -1) {
		printf("Failed to load disk");
		return {};
	}

	uint32_t begin = (getTrackBegin(pos) + tracks[pos].pregap).toLba();
	uint32_t loc = location.toLba();

    if (loc < begin) {
        return buffer;
    }

    auto f = fopen(tracks[pos].filePath.c_str(), "rb");
    if (!f) {
        std::printf("Unable to load file %s\n", tracks[pos].filePath.c_str());
        return {};
    }

	auto seek = location - (getTrackBegin(pos) + tracks[pos].pregap);

    //long offset = tracks[pos].fileDataOffset + (long)(tracks[pos].trackIndex + (loc - begin)) * tracks[pos].modeType;
	long offset = 0 + seek.toLba() * Sector::RAW_BUFFER;

    fseek(f, offset, SEEK_SET);
    fread(buffer.data(), 1, std::min<size_t>(tracks[pos].modeType, buffer.size()), f);
    fclose(f);

	return buffer;
}*/

std::vector<uint8_t> Disk::read(Location location) {
	auto buffer = std::vector<uint8_t>(Sector::RAW_BUFFER, 0);

	int pos = getTrackPosition(location);

	if(pos == -1) {
		printf("");
		return {};
	}

	static auto f = fopen(tracks[0].filePath.c_str(), "rb");
	if (!f) {
		std::printf("Unable to load file %s\n", tracks[0].filePath.c_str());
		return {};
	}

	auto seek = location - (getTrackBegin(pos) + tracks[pos].pregap);

	// 0 -> track num
	long offset = 0 + seek.toLba() * Sector::RAW_BUFFER;

	fseek(f, offset, SEEK_SET);
	fread(buffer.data(), Sector::RAW_BUFFER, 1, f);

	return buffer;
}

bool Disk::isAudio(Location location) {
	int pos = getTrackPosition(location);
	return pos >= 0 && tracks[pos].mode == "AUDIO";
}

void Disk::set(const std::string& path) {
	tracks.clear();
	tracks = _builder.parseFile(path);

	// Will try to load in the images for a HUD
	Track curTrack = tracks[0]; // TODO: For now just assume it's always in track 0 (game data)

	// Read entire file data
	std::ifstream file(Emulator::Utils::FileManager::resolvePath(curTrack.filePath), std::ios::binary | std::ios::ate);

	if (!file.is_open())
		throw std::runtime_error("Failed to locate " + curTrack.filePath);

	size_t fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(fileSize);
	file.read(reinterpret_cast<char*>(data.data()), fileSize);
	file.close();

	if (fileSize < curTrack.sectorCount * 2352)
		throw std::runtime_error("Track smaller than reported sector count");

	std::vector<uint8_t> iso;

	iso.reserve(curTrack.sectorCount * 2048);

	for (size_t s = 0; s < curTrack.sectorCount; s++) {
		const uint8_t* sector = &data[s * 2352];

		iso.insert(iso.end(), sector + 24, sector + 24 + 2048);
	}

	const char* needle = "cdrom:";

	auto it = std::search(iso.begin(), iso.end(), needle, needle + 6);

	size_t offset = std::distance(iso.begin(), it) + 6;

	std::string raw;

	while (offset < iso.size()) {
		char c = (char)iso[offset];

		if (c == '\r' || c == '\n' || c == ';' || c == '\0')
			break;

		if (c != '\\' && c != '/')
			raw += c;

		offset++;
	}

	std::string serial;

	for (char c : raw) {
		if (isalnum((unsigned char)c))
			serial += toupper((unsigned char)c);
	}

	//std::string serialPath = serial.substr(0, 4) + "-" + serial.substr(4, 5);

	//printf("Path: %s\n", serialPath.c_str());

	/*const uint8_t* block = &card[0x2000 * blockIndex];

	if (block[0] != 'S' || block[1] != 'C')
		return;

	int frames = block[0x02] & 0x0F;

	uint16_t clut[16];

	for (int i = 0; i < 16; i++)
		clut[i] = block[0x60 + i * 2] | (block[0x61 + i * 2] << 8);

	const uint8_t* icon = &block[0x80];

	uint32_t pixels[16 * 16];

	uint32_t ToRGBA(uint16_t c) {
		uint8_t r = (c & 0x1F) << 3;
		uint8_t g = ((c >> 5) & 0x1F) << 3;
		uint8_t b = ((c >> 10) & 0x1F) << 3;
		uint8_t a = (c == 0) ? 0 : 255;

		return r | (g << 8) | (b << 16) | (a << 24);
	}

	for (int i = 0; i < 128; i++) {
		uint8_t byte = icon[i];

		uint16_t lo = clut[byte & 0x0F];
		uint16_t hi = clut[byte >> 4];

		pixels[i * 2 + 0] = ToRGBA(lo);
		pixels[i * 2 + 1] = ToRGBA(hi);
	}*/

	/*std::string needle = "TIM";

	auto it = std::search(data.begin(), data.end(), needle.begin(), needle.end());

	if (it != data.end()) {
		size_t offset = std::distance(data.begin(), it);

		printf("FOUND at 0x%zX\n", offset);

		for (size_t i = offset; i < offset + 256 && i < data.size(); i++) {
			int8_t c = data[i];
			printf("%02X ", static_cast<uint8_t>(c));
			/*if (c < 32 || c >= 127) {
				continue;
			}

			printf("%c", c);#1#
		}

		printf("\n");
	}*/

	/*const uint8_t needle[] = { 0x10, 0x00, 0x00, 0x00 };

	auto it = std::search(data.begin(), data.end(), needle, needle + 4);

	if (it != data.end()) {
		size_t offset = std::distance(data.begin(), it);

		printf("FOUND at 0x%zX\n", offset);

		for (size_t i = offset; i < offset + 256 && i < data.size(); i++) {
			int8_t c = data[i];
			printf("%02X ", static_cast<uint8_t>(c));
		}

		printf("\n");
	}*/

	/*const uint8_t needle[] = { 0x10, 0x00, 0x00, 0x00 };

	auto it = iso.begin();

	while (true) {
		it = std::search(it, iso.end(), needle, needle + 4);

		if (it == iso.end())
			break;

		size_t offset = std::distance(iso.begin(), it);

		it += 4;

		if (offset + 0x14 > iso.size())
			break;

		uint32_t flags = *reinterpret_cast<const uint32_t*>(&iso[offset + 0x04]);
		uint32_t size = *reinterpret_cast<const uint32_t*>(&iso[offset + 0x08]);
		uint16_t x = *reinterpret_cast<const uint16_t*>(&iso[offset + 0x0C]);
		uint16_t y = *reinterpret_cast<const uint16_t*>(&iso[offset + 0x0E]);
		uint16_t xsiz = *reinterpret_cast<const uint16_t*>(&iso[offset + 0x10]);
		uint16_t ysiz = *reinterpret_cast<const uint16_t*>(&iso[offset + 0x12]);

		uint32_t type = flags & 7;

		if (type > 5)
			continue;

		if (flags & ~0x0Fu)
			continue;

		if (xsiz == 0 || ysiz == 0 || xsiz > 1024 || ysiz > 512)
			continue;

		if (x >= 1024 || y >= 512)
			continue;

		if (size != (uint32_t)xsiz * 2 * ysiz + 0x0C)
			continue;

		if (offset + 0x08 + size > iso.size())
			continue;

		printf("TIM at 0x%zX type=%u clut=%u %ux%u\n", offset, type, (flags >> 3) & 1, xsiz, ysiz);
	}*/
}

Location Disk::getSize() {
	size_t frames = 75 * 2;
	
	for(auto t : tracks) {
		frames += t.pregap.toLba() + t.sectorCount;
	}
	
	return Location::fromLBA(frames);
}

Location Disk::getTrackStart(int track) {
	return getTrackBegin(track) + tracks[track].pregap;
}

int Disk::getTrackPosition(Location loc) {
	for(int i = 0; i < tracks.size(); i++) {
		auto begin = getTrackBegin(i);
		auto end = getTrackLength(i);
		
		if(loc >= begin && loc < begin + end) {
			return i;
		}
	}
	
	return -1;
}

Location Disk::getTrackBegin(int track) {
	size_t total = 75 * 2;
	
	for (int i = 0; i < track; i++) {
		total += tracks[i].pregap.toLba() + tracks[i].sectorCount;
	}
	
	return Location::fromLBA(total);
}

Location Disk::getTrackLength(int track) {
	return Location::fromLBA(tracks[track].pregap.toLba() + tracks[track].sectorCount);
}
