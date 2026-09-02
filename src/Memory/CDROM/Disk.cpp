#include "Disk.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "CDROM.h"
#include "../../Utils/FileSystem/FileManager.h"

static uint32_t readLe32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static bool readIsoSector(std::ifstream& file, uint32_t lba, uint8_t* out2048) {
	file.clear();
	file.seekg(static_cast<std::streamoff>(lba) * Sector::RAW_BUFFER + 24, std::ios::beg);
	file.read(reinterpret_cast<char*>(out2048), 2048);

	return file.gcount() == 2048;
}

static std::string readSystemCnfViaIso9660(std::ifstream& file) {
	uint8_t pvd[2048];

	if (!readIsoSector(file, 16, pvd))
		return "";

	if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0)
		return "";

	const uint8_t* rootRecord = pvd + 156;
	uint32_t       rootLba    = readLe32(rootRecord + 2);
	uint32_t       rootSize   = readLe32(rootRecord + 10);

	uint32_t dirSectors = std::min<uint32_t>((rootSize + 2047) / 2048, 16);
	if (dirSectors == 0)
		return "";

	std::vector<uint8_t> dir(static_cast<size_t>(dirSectors) * 2048);

	for (uint32_t s = 0; s < dirSectors; s++) {
		if (!readIsoSector(file, rootLba + s, dir.data() + s * 2048))
			return "";
	}

	size_t offset = 0;

	while (offset + 33 <= dir.size()) {
		uint8_t recordLen = dir[offset];

		if (recordLen == 0) {
			offset = ((offset / 2048) + 1) * 2048;
			continue;
		}

		uint8_t     nameLen = dir[offset + 32];
		std::string name(reinterpret_cast<char*>(&dir[offset + 33]), nameLen);

		size_t versionPos = name.find(';');
		if (versionPos != std::string::npos)
			name = name.substr(0, versionPos);

		std::string upperName;
		for (char c : name)
			upperName += toupper((unsigned char)c);

		if (upperName == "SYSTEM.CNF") {
			uint32_t extentLba  = readLe32(&dir[offset + 2]);
			uint32_t extentSize = readLe32(&dir[offset + 10]);

			uint32_t fileSectors = std::max<uint32_t>(1, std::min<uint32_t>((extentSize + 2047) / 2048, 4));

			std::vector<uint8_t> content(static_cast<size_t>(fileSectors) * 2048);

			for (uint32_t s = 0; s < fileSectors; s++) {
				if (!readIsoSector(file, extentLba + s, content.data() + s * 2048))
					break;
			}

			return Disk::extractSerialFromIso(content);
		}

		offset += recordLen;
	}

	return "";
}

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

	const auto& track = tracks[pos];

	static std::string openFilePath;
	static FILE* f = nullptr;

	if (f == nullptr || openFilePath != track.filePath) {
		if (f) {
			fclose(f);
		}

		f = fopen(track.filePath.c_str(), "rb");
		openFilePath = track.filePath;
	}

	if (!f) {
		std::printf("Unable to load file %s\n", track.filePath.c_str());
		return {};
	}

	auto seek = location - (getTrackBegin(pos) + track.start());
	long seekSectors = seek.toLba();

	if (seekSectors < 0) {
		return buffer;
	}

	long offset = static_cast<long>(track.fileDataOffset) + (seekSectors * Sector::RAW_BUFFER);

	fseek(f, offset, SEEK_SET);
	fread(buffer.data(), 1, Sector::RAW_BUFFER, f);
	//fclose(f);

	return buffer;
}

bool Disk::isAudio(Location location) {
	int pos = getTrackPosition(location);
	return pos >= 0 && tracks[pos].mode == "AUDIO";
}

std::string Disk::extractSerialFromIso(const std::vector<uint8_t>& iso) {
	const char* needle = "cdrom:";

	auto it = std::search(iso.begin(), iso.end(), needle, needle + 6);

	size_t offset = std::distance(iso.begin(), it) + 6;

	std::string raw;

	while (offset < iso.size()) {
		char c = (char)iso[offset];

		if (c == '\r' || c == '\n' || c == ';' || c == '\0')
			break;

		raw += c;

		offset++;
	}

	size_t lastSeparator = raw.find_last_of("\\/");
	std::string fileName = (lastSeparator == std::string::npos) ? raw : raw.substr(lastSeparator + 1);

	std::string serial;

	for (char c : fileName) {
		if (isalnum((unsigned char)c))
			serial += toupper((unsigned char)c);
	}

	return serial;
}

std::string Disk::readSerial(const std::string& path) {
	TrackBuilder builder;
	std::vector<Track> tracks;

	try {
		tracks = builder.parseFile(path);
	} catch (...) {
		return "";
	}

	if (tracks.empty())
		return "";

	const Track& track = tracks[0];

	std::ifstream file(Emulator::Utils::FileManager::resolvePath(track.filePath), std::ios::binary);

	if (!file.is_open())
		return "";

	std::string serial = readSystemCnfViaIso9660(file);

	if (!serial.empty())
		return serial;

	file.clear();
	file.seekg(0, std::ios::beg);

	uint32_t sectorCount = std::min<uint32_t>(track.sectorCount, 150);

	std::vector<uint8_t> raw(static_cast<size_t>(sectorCount) * Sector::RAW_BUFFER);
	file.read(reinterpret_cast<char*>(raw.data()), raw.size());

	std::vector<uint8_t> iso;
	iso.reserve(static_cast<size_t>(sectorCount) * 2048);

	for (size_t s = 0; s < sectorCount; s++) {
		const uint8_t* sector = &raw[s * Sector::RAW_BUFFER];
		iso.insert(iso.end(), sector + 24, sector + 24 + 2048);
	}

	return extractSerialFromIso(iso);
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

	std::string serial = extractSerialFromIso(iso);

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
		frames += t.start().toLba() + t.sectorCount;
	}
	
	return Location::fromLBA(frames);
}

Location Disk::getTrackStart(int track) {
	return getTrackBegin(track) + tracks[track].start();
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
		total += tracks[i].start().toLba() + tracks[i].sectorCount;
	}
	
	return Location::fromLBA(total);
}

Location Disk::getTrackLength(int track) {
	return Location::fromLBA(tracks[track].start().toLba() + tracks[track].sectorCount);
}
