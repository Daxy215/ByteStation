#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace Emulator {
	namespace Utils {
		class FileManager {
		public:
			static std::filesystem::path exeDir() {
				return std::filesystem::canonical("/proc/self/exe").parent_path();
			}
			
			static std::filesystem::path resolvePath(const std::string& path) {
				return exeDir() / path;
			}
			
			static std::vector<uint8_t> loadFile(const std::string& path) {
				std::ifstream stream(resolvePath(path), std::ios::binary | std::ios::ate);
				
				if (!stream.good()) {
					std::cerr << "Cannot read from file: " << path << '\n';
					return {};
				}
				
				auto fileSize = stream.tellg();
				stream.seekg(0, std::ios::beg);
				
				std::vector<uint8_t> exe(fileSize);
				
				if (!stream.read(reinterpret_cast<char*>(exe.data()), fileSize)) {
					std::cerr << "Error reading file!" << '\n';
					
					return {};
				}
				
				return exe;
			}
		};
	}
}
