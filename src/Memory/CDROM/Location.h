#pragma once
#include <cstdint>

class Location {
public:
	Location() = default;
	Location(int minutes, int seconds, int sectors)
		: minutes(minutes),
		  seconds(seconds),
		  sectors(sectors) {
		
	}
	
	void setLocation(int minutes, int seconds, int sectors) {
		this->minutes = minutes;
		this->seconds = seconds;
		this->sectors = sectors;
		
		//location = sectors + (seconds * 75) + (minutes * 60 * 70);
	}
	
	uint32_t toLba() const { return (minutes * 60 * 75) + (seconds * 75) + sectors; }
	
	static Location fromLBA(uint32_t lba) {
		int mm = (int)lba / 60 / 75;
		int ss = ((int)lba % (60 * 75)) / 75;
		int ff = (int)lba % 75;
		
		return {mm, ss, ff};
	}

public:
	Location operator+(const Location& p) const {
		return fromLBA(toLba() + p.toLba());
	}
	
	Location operator-(const Location& p) const {
		return fromLBA(toLba() - p.toLba());
	}
	
	bool operator>=(const Location& loc) const {
		return toLba() >= loc.toLba();
	}
	
	bool operator<(const Location& loc) const {
		return toLba() < loc.toLba();
	}
	
public:
	int minutes = 0;
	int seconds = 0;
	int sectors = 0;
	
public:
	//uint32_t location = 0;
	
};
