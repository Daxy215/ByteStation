#include "Disk.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <cstdio>

#include "CDROM.h"

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
