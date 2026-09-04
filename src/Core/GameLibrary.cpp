#include "GameLibrary.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include <GL/glew.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../Utils/stb_image.h"

#include "../Memory/CDROM/Disk.h"
#include "../Utils/FileSystem/FileManager.h"

using json = nlohmann::json;

static bool downloadFile(const std::string& url, const std::string& outPath) {
#ifdef _WIN32
	std::string commandLine = "curl.exe -fsSL -o \"" + outPath + "\" \"" + url + "\"";

	STARTUPINFOA startupInfo{};
	startupInfo.cb = sizeof(startupInfo);

	PROCESS_INFORMATION processInfo{};

	if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
	                     nullptr, nullptr, &startupInfo, &processInfo))
		return false;

	WaitForSingleObject(processInfo.hProcess, INFINITE);

	DWORD exitCode = 1;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);

	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);

	return exitCode == 0;
#else
	pid_t pid = fork();

	if (pid == 0) {
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
		execlp("curl", "curl", "-fsSL", "-o", outPath.c_str(), url.c_str(), (char*)nullptr);
		_exit(127);
	}

	if (pid < 0)
		return false;

	int status = 0;
	waitpid(pid, &status, 0);

	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static GLuint uploadCoverTexture(const std::string& path) {
	int width, height, channels;

	unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

	if (!data)
		return 0;

	GLuint texture;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureStorage2D(texture, 1, GL_RGBA8, width, height);
	glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);

	stbi_image_free(data);

	return texture;
}

static std::string normalizeSerial(const std::string& serial) {
	std::string normalized;

	for (char c : serial) {
		if (isalnum((unsigned char)c))
			normalized += toupper((unsigned char)c);
	}

	return normalized;
}

void GameLibrary::loadDatabase(const std::string& jsonPath) {
	titlesBySerial.clear();

	std::ifstream file(Emulator::Utils::FileManager::resolvePath(jsonPath));

	if (!file.is_open())
		return;

	try {
		json j;
		file >> j;

		for (auto& [serial, title] : j.items())
			titlesBySerial[normalizeSerial(serial)] = title.get<std::string>();
	} catch (const json::exception&) {
		titlesBySerial.clear();
	}
}

void GameLibrary::scan(const std::string& romsDir) {
	entries.clear();

	std::filesystem::path baseDir = Emulator::Utils::FileManager::resolvePath(romsDir);

	if (!std::filesystem::exists(baseDir))
		return;

	for (const auto& dirEntry : std::filesystem::directory_iterator(baseDir)) {
		if (!dirEntry.is_directory())
			continue;

		std::string discPath;
		std::string binPath;

		for (const auto& fileEntry : std::filesystem::directory_iterator(dirEntry.path())) {
			std::string extension = fileEntry.path().extension().string();

			if (extension == ".cue") {
				discPath = fileEntry.path().string();
				break;
			}

			if (extension == ".bin" && binPath.empty())
				binPath = fileEntry.path().string();
		}

		if (discPath.empty())
			discPath = binPath;

		if (discPath.empty())
			continue;

		GameEntry entry;
		entry.discPath = discPath;
		entry.serial   = Disk::readSerial(discPath);

		auto it     = titlesBySerial.find(entry.serial);
		entry.title = (it != titlesBySerial.end()) ? it->second : dirEntry.path().filename().string();

		entries.push_back(std::move(entry));
	}

	std::sort(entries.begin(), entries.end(), [](const GameEntry& a, const GameEntry& b) {
		return a.title < b.title;
	});

	for (size_t i = 0; i < entries.size(); i++)
		requestCover(entries[i]);
}

static std::string dashedSerial(const std::string& serial) {
	if (serial.size() <= 4)
		return serial;

	return serial.substr(0, 4) + "-" + serial.substr(4);
}

void GameLibrary::requestCover(GameEntry& entry) {
	if (entry.serial.empty() || entry.coverRequested)
		return;

	entry.coverRequested = true;

	size_t index = &entry - entries.data();

	std::filesystem::path cachePath = Emulator::Utils::FileManager::resolvePath("../../Resources/Covers/" + entry.serial + ".jpg");
	entry.coverCachePath            = cachePath.string();

	if (std::filesystem::exists(cachePath)) {
		std::lock_guard<std::mutex> lock(readyMutex);
		readyCovers.emplace_back(index, entry.coverCachePath);

		return;
	}

	std::string url = "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/" + dashedSerial(entry.serial) + ".jpg";
	std::string finalPath = entry.coverCachePath;
	std::error_code ec;
	std::filesystem::create_directories(cachePath.parent_path(), ec);

	std::thread([this, index, url, finalPath]() {
		std::string tmpPath = finalPath + ".tmp";

		if (downloadFile(url, tmpPath)) {
			std::error_code renameEc;
			std::filesystem::rename(tmpPath, finalPath, renameEc);

			if (!renameEc) {
				std::lock_guard<std::mutex> lock(readyMutex);
				readyCovers.emplace_back(index, finalPath);
				return;
			}
		}

		std::error_code removeEc;
		std::filesystem::remove(tmpPath, removeEc);
	}).detach();
}

void GameLibrary::update() {
	std::vector<std::pair<size_t, std::string>> ready;

	{
		std::lock_guard<std::mutex> lock(readyMutex);
		ready.swap(readyCovers);
	}

	for (auto& [index, path] : ready) {
		if (index >= entries.size())
			continue;

		entries[index].coverTexture = uploadCoverTexture(path);
	}
}
