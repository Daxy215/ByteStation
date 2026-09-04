#pragma once
#include <string>

#include "Sector.h"
#include "TrackBuilder.h"

#pragma warning(disable : 4996)

class Location;

class Disk {
public:
	Disk();
	
	std::vector<uint8_t> read(Location location);
	bool isAudio(Location location);

	void set(const std::string& path);

public:
	static std::string extractSerialFromIso(const std::vector<uint8_t>& iso);
	static std::string readSerial(const std::string& path);

public:
	Location getSize();
	Location getTrackStart(int i);

public:
	int getTrackPosition(Location location);
	
	Location getTrackBegin(int track);
	Location getTrackLength(int track);
	
private:
	TrackBuilder _builder;
	
public:
	static std::string GAME_NAME;
	std::vector<Track> tracks;
};
