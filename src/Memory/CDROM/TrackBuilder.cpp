#include "TrackBuilder.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "Sector.h"
#include "../../Utils/FileSystem/FileManager.h"

struct FileAudioData {
    uint32_t offset = 0;
    uint32_t size = 0;
};

static uint32_t parseCueIndexLba(const std::string& value) {
    int minutes = std::stoi(value.substr(0, 2));
    int seconds = std::stoi(value.substr(3, 2));
    int frames = std::stoi(value.substr(6, 2));
    
    return static_cast<uint32_t>((minutes * 60 * 75) + (seconds * 75) + frames);
}

static uint32_t readLe32(std::ifstream& file) {
    uint8_t bytes[4]{};
    file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));

    return static_cast<uint32_t>(bytes[0])
           | (static_cast<uint32_t>(bytes[1]) << 8)
           | (static_cast<uint32_t>(bytes[2]) << 16)
           | (static_cast<uint32_t>(bytes[3]) << 24);
}

static FileAudioData getFileAudioData(const std::string& path, bool waveFile) {
    return FileAudioData {0, 0};
}

std::vector<Track> TrackBuilder::parseFile(const std::string& filePath) {
	if(filePath.empty()) {
		return {};
	}
	
	std::filesystem::path path = filePath;
	
	std::string extension = path.extension().string();
	
	if(extension == (".cue")) {
		std::cerr << "Building a cue file\n";
		return parseCueFile(filePath);
	} else if(extension == (".bin")) {
		std::cerr << "Building a bin file\n";
	} else {
		throw std::runtime_error("Unsupported disk type");
	}
	
	return {};
}

std::vector<Track> TrackBuilder::parseCueFile(const std::string& path) {
	std::filesystem::path filePath = Emulator::Utils::FileManager::resolvePath(path);
	
	std::ifstream cueFile(filePath);
    std::vector<Track> tracks;
	
	if(!cueFile) {
		std::cerr << "Error; Couldn't find disk with the path of: " << path << "\n";
		
		// Print current directory for debugging
		std::filesystem::path current_path = std::filesystem::current_path();
		
		std::cerr << "Current path: " << current_path << std::endl;
		
		throw std::runtime_error("Couldn't find disk through the provided path.");
	}
    
    std::string currentFile;
    std::string currentFileType;
    std::string line;

    while (std::getline(cueFile, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line.find("FILE") == 0) {
            size_t pos = line.find('"');
            size_t endPos = line.find('"', pos + 1);

            std::string fileName = line.substr(pos + 1, endPos - pos - 1);

            currentFile = (filePath.parent_path() / fileName).string();
            currentFileType = line.substr(endPos + 1);
            trim(currentFileType);

            continue;
        }
        
        if (line.find("TRACK") == 0 && !currentFile.empty()) {
            Track track;
            track.filePath = currentFile;
            track.type = line;
            
            size_t modePos = line.find("MODE");
            size_t audioPos = line.find("AUDIO");

            if (audioPos != std::string::npos) {
                track.mode = "AUDIO";
                track.modeType = 1; // TODO:

            	//auto audioData = getFileAudioData(track.filePath, track.mode == "AUDIO");

            	// TODO:
            	//continue;
            } else if (modePos != std::string::npos) {
                track.mode = line.substr(modePos, 5);
                track.modeType = std::stoi(line.substr(modePos + 6));
            } else {
				printf("Unknown cdrom format\n");

                continue;
            }

        	track.fileDataOffset = 0; // TODO:
            track.sectorCount = static_cast<uint32_t>(getFileSize(currentFile) / Sector::RAW_BUFFER);

        	printf("Loaded track %lu contains %d sectors (%s)\n", (tracks.size() + 1), track.sectorCount, track.mode.c_str());
            
            tracks.push_back(track);

            continue;
        }
        
        if (line.find("INDEX 01") == 0 && !tracks.empty()) {
            size_t pos = line.find_last_of(' ');

            if (pos != std::string::npos) {
                tracks.back().trackIndex = parseCueIndexLba(line.substr(pos + 1));
            }
        }
    }

	/**
	 * Wtf was I doing??
	 */
    /*std::unordered_map<std::string, uint32_t> fileSectors;
    for (const auto& track : tracks) {
        if (fileSectors.find(track.filePath) != fileSectors.end()) {
            continue;
        }

        fileSectors[track.filePath] = static_cast<uint32_t>(getFileSize(track.filePath) / track.modeType);
    }
    
    for (size_t i = 0; i < tracks.size(); i++) {
        uint32_t end = fileSectors[tracks[i].filePath];

        for (size_t j = i + 1; j < tracks.size(); j++) {
            if (tracks[j].filePath == tracks[i].filePath) {
                end = tracks[j].trackIndex;
                break;
            }
        }

        tracks[i].sectorCount = end > tracks[i].trackIndex
                                ? end - tracks[i].trackIndex
                                : fileSectors[tracks[i].filePath];
    }*/
	
	return tracks;
}

void TrackBuilder::parseBinFile(const std::string& path) {
	std::ifstream binFile(Emulator::Utils::FileManager::resolvePath(path));

	if (!binFile.is_open()) {
		std::cerr << "Error: Could not find bin file." << '\n';
		return;
	}
	
	std::cerr << "";
}

uint32_t TrackBuilder::getFileSize(const std::string &path) {
	std::ifstream file(Emulator::Utils::FileManager::resolvePath(path), std::ios::binary | std::ios::ate);
	if (!file.is_open())
		throw std::runtime_error("Failed to locate " + path);

	return file.tellg();
}

void TrackBuilder::trim(std::string& str) {
	const std::string whitespace = " \t\n\r";
	str.erase(0, str.find_first_not_of(whitespace));
	str.erase(str.find_last_not_of(whitespace) + 1);
}
