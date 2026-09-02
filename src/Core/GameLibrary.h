#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

struct GameEntry {
	std::string serial;
	std::string title;
	std::string discPath;
	std::string coverCachePath;
	GLuint      coverTexture   = 0;
	bool        coverRequested = false;
};

class GameLibrary {
public:
	void loadDatabase(const std::string& jsonPath);
	void scan(const std::string& romsDir);
	void update();

	std::vector<GameEntry>& games() { return entries; }

private:
	void requestCover(GameEntry& entry);

private:
	std::unordered_map<std::string, std::string> titlesBySerial;
	std::vector<GameEntry> entries;

	std::mutex readyMutex;
	std::vector<std::pair<size_t, std::string>> readyCovers;
};
