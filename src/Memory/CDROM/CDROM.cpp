#include "CDROM.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <limits>

std::deque<std::pair<int16_t, int16_t>> CDROM::audioSamples;

CDROM::CDROM() : _readSector(Sector::RAW_BUFFER), _sector(Sector::RAW_BUFFER) {
	
}

void CDROM::step(uint32_t cycles) {
	/*if(!interrupts.is_empty()) {
		auto& in = interrupts.ref();

		if(in.delay > 0)
			in.delay -= cycles;

		if(in.delay <= 0 && !in.fired) {
			in.fired = true;
			transmittingCommand = false;

			if((IE & 7) & (in._interrupt & 7)) {
				IRQ::trigger(IRQ::Interrupt::CDROM);
			}
		}
	}*/

	if (!interrupts.is_empty()) {
		interrupts.ref().delay -= cycles;

		if (interrupts.peek().delay <= 0) {
			transmittingCommand = false;

			if ((IE & 7) & (interrupts.peek()._interrupt & 7)) {
				IRQ::trigger(IRQ::Interrupt::CDROM);
			}
		}
	}
	
	/**
	 * 1. Command busy flag set immediately.
	 * 2. Response FIFO is populated.
	 * 3. Command is being processed.
	 * 4. Command busy flag is unset and parameter fifo is cleared.
	 * 5. Shortly after (around 1000-6000 cycles later), CDROM IRQ is fired.
	 */
	busyFor -= cycles;

	if(busyFor < 0)
		transmittingCommand = false;
	
	const int sectorsPerSecond = mode.speed ? 150 : 75;
	const int cyclesPerSector = 33868800 / sectorsPerSecond; // CPU CLOCK
	
	this->cycles += cycles;
	
	for(int i = 0; i < this->cycles / cyclesPerSector; i++) {
		handleSector();
	}
	
	this->cycles %= cyclesPerSector;
}

uint32_t CDROM::cyclesUntilNextInterrupt() const {
	if (interrupts.is_empty())
		return std::numeric_limits<uint32_t>::max();

	int32_t delay = interrupts.peek().delay;
	return delay > 0 ? static_cast<uint32_t>(delay) : 1;
}

void CDROM::handleSector() {
	if(!_stats.read && !_stats.play)
		return;

	if (_stats.read && !interrupts.is_empty())
		return;
	
	/**
	 * Mode2/Form1 (CD-XA)
	 * 000h 0Ch  Sync   (00h,FFh,FFh,FFh,FFh,FFh,FFh,FFh,FFh,FFh,FFh,00h)
	 * 00Ch 4    Header (Minute,Second,Sector,Mode=02h)
	 * 010h 4    Sub-Header (File, Channel, Submode AND DFh, Codinginfo)
	 * 014h 4    Copy of Sub-Header
	 * 018h 800h Data (2048 bytes)
	 * 818h 4    EDC (checksum across [010h..817h])
	 * 81Ch 114h ECC (error correction codes)
	 */
	const std::array<uint8_t, 12> sync = {{0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}};
	
	Location pos = Location::fromLBA(readLocation);
	auto rawSector = _disk.read(pos);
	_readSector.set(rawSector);
	
	readLocation++;

	if (_stats.play && _disk.isAudio(pos)) {
		int trackNum = _disk.getTrackPosition(Location::fromLBA(readLocation - 1));

		if (this->mode.report) {
			// Report --> INT1(stat,track,index,mm/amm,ss+80h/ass,sect/asect,peaklo,peakhi)
			auto posInTrack = pos - _disk.getTrackStart(trackNum);
			uint8_t sector = pos.sectors;

			auto toBcd = [](uint8_t b) -> uint8_t {
				return ((b / 10) << 4) | (b % 10);
			};

			auto cddaReport = [&](bool isTrack) {
				INT(1, 1000);
				addResponse(_stats._reg);           // stat
				addResponse(toBcd(trackNum + 1)); // track
				addResponse(0x01);                  // index

				if (isTrack) {
					// mm/ss+80h/sect are returned on asect=10h,30h,50h,70h  ;-within current track
					addResponse(toBcd(posInTrack.minutes));        // minute (track)
					addResponse(toBcd(posInTrack.seconds) | 0x80); // second (track)
					addResponse(toBcd(posInTrack.sectors));        // sector (track)
				} else {
					//   amm/ass/asect are returned on asect=00h,20h,40h,60h   ;-absolute time
					addResponse(toBcd(pos.minutes)); // minute (disc)
					addResponse(toBcd(pos.seconds)); // second (disc)
					addResponse(toBcd(pos.sectors)); // sector (disc)
				}

				// generated via CXD2510Q Signal Processor
				addResponse(rand());  // peaklo
				addResponse(rand());  // peakhi
			};

			if (sector % 0x20 == 0) {
				cddaReport(false);
			} else if ((sector - 0x10) % 0x20 == 0) {
				cddaReport(true);
			}
		}

		if (/*!mute && */mode.cdda) {
			if (rawSector.size() >= sync.size() && memcmp(rawSector.data(), sync.data(), sync.size()) == 0) {
				printf("CDROM; Trying to read Data track as audio\n");
			} else {
				queueCdAudioSector(rawSector);
			}
		}
	} else if (_stats.read && !_disk.isAudio(pos)) { // TODO: Im lazy..
		if (memcmp(_readSector.data(), sync.data(), sync.size()) != 0) {
			// TODO; This.. does happen on Tekken 3
			//printf("CDROM; Sync failed\n");

			//assert(false);
			return;
		}

		INT(1, 0);
		addResponse(_stats._reg);

		// uint8_t minute = rawSector[12];
		// uint8_t second = rawSector[13];
		// uint8_t frame = rawSector[14];

		uint8_t mode = _readSector.loadAt(15);

		// 0-7 File Number    (00h..FFh) (for Audio/Video Interleave, see below)
		uint8_t file = _readSector.loadAt(16);

		/*
		 * 0-4 Channel Number (00h..1Fh) (for Audio/Video Interleave, see below)
		 * 5-7 Should be always zero
		 */
		uint8_t channel = _readSector.loadAt(17) & 0x0F;

		/**
		 * 0   End of Record (EOR) (all Volume Descriptors, and all sectors with EOF)
		 * 1   Video     ;\Sector Type (usually ONE of these bits should be set)
		 * 2   Audio     ; Note: PSX .STR files are declared as Data (not as Video)
		 * 3   Data      ;/
		 * 4   Trigger           (for application use)
		 * 5   Form2             (0=Form1/800h-byte data, 1=Form2, 914h-byte data)
		 * 6   Real Time (RT)
		 * 7   End of File (EOF) (or end of Directory/PathTable/VolumeTerminator)
		 */
		auto submode = (_readSector.loadAt(18));

		/**
		 * 0-1 Mono/Stereo     (0=Mono, 1=Stereo, 2-3=Reserved)
		 * 2-2 Sample Rate     (0=37800Hz, 1=18900Hz, 2-3=Reserved)
		 * 4-5 Bits per Sample (0=Normal/4bit, 1=8bit, 2-3=Reserved)
		 * 6   Emphasis        (0=Normal/Off, 1=Emphasis)
		 * 7   Reserved        (0)
		 */
		auto codinginfo = (_readSector.loadAt(19));

		// Real Time (RT) && Audio
		if ((submode >> 6 & 1) == 1 && (submode >> 2 & 1) == 1) {
			uint8_t isStereo = (codinginfo & 0x03) == 1;
			/*uint8_t is18900Hz = (codinginfo >> 2 & 1);
			uint8_t is8Bits = ((codinginfo >> 4) & 0x03) == 1;
			uint8_t isEmphasis = (codinginfo >> 6 & 1);*/

			if (this->mode.xaAdpcm && !mute) {
				// TODO; handle filter
				//assert(!this->mode.xaFilter);
				if (this->mode.xaFilter) {
					if (this->filter.file != file || this->filter.channel != channel) {
						return;
					}
				}

				//printf("Need to decode is18900Hz(%x) is8bits(%x) isstereo(%x) isemphasis(%x)\n", is18900Hz, is8Bits, isStereo, isEmphasis);
				// Pink panther uses(18900Hz, 4bits, mono, Emphasis=normal)

				uint8_t pos_xa_adpcm_table[5] = { 0, 60, 115,  98, 122 };
				int8_t  neg_xa_adpcm_table[5] = { 0, 0 , -52, -55, -60 };

				/**
				 * Each sector consists of 12h(18) 128-byte portions (=900h bytes)
				 * (the remaining 14h bytes of the sectors 914h-byte data region are 00h filled).
				 */
				uint8_t offset = 24; // 12+4+8 ;skip sync,header,subheader
				auto src = rawSector.data() + offset;

				std::vector<int16_t> left;
				std::vector<int16_t> right;

				//for (int block = 0; block < 18; block++) {
					/**
					 * 00h..03h Copy of below 4 bytes (at 04h..07h)
  					 * 04h      Header for 1st Block/Mono, or 1st Block/Left
  					 * 05h      Header for 2nd Block/Mono, or 1st Block/Right
  					 * 06h      Header for 3rd Block/Mono, or 2nd Block/Left
  					 * 07h      Header for 4th Block/Mono, or 2nd Block/Right
  					 * 08h      Header for 5th Block/Mono, or 3rd Block/Left  ;\unknown/unused
  					 * 09h      Header for 6th Block/Mono, or 3rd Block/Right ; for 8bit ADPCM
  					 * 0Ah      Header for 7th Block/Mono, or 4th Block/Left  ; (maybe 0, or maybe
  					 * 0Bh      Header for 8th Block/Mono, or 4th Block/Right ;/copy of above)
  					 * 0Ch..0Fh Copy of above 4 bytes (at 08h..0Bh)
					 */
					auto decode_28_nibbles = [this, &pos_xa_adpcm_table, &neg_xa_adpcm_table](const uint8_t *data, const uint8_t blk, uint8_t nibble, std::vector<int16_t> &dst, int32_t &old, int32_t &older) {
						/**
						 * 0-3 Shift  (0..12) (0=Loudest) (13..15=Reserved/Same as 9)
						 * 4-5 Filter (0..3) (only four filters, unlike SPU-ADPCM which has five)
						 * 6-7 Unused (should be 0)
						 */
						auto shift = 12 - (data[4 + blk * 2 + nibble] & 0xF);
						auto filter = (data[4+blk*2+nibble] & 0x30) >> 4;
						//if (shift > 12) shift = 9; // 13..15 = same as 9

						auto f0 = pos_xa_adpcm_table[filter];
						auto f1 = neg_xa_adpcm_table[filter];

						auto signed4bit = [](uint8_t nibble) -> int16_t {
							return (nibble & 0x07) - (nibble & 0x08);
						};

						for (int j = 0; j < 28; j++) {
							auto t = signed4bit((data[16+blk+j*4] >> (nibble*4)) & 0x0F);
							auto sample = (t << shift) + ((old*f0 + older*f1+32) / 64);
							sample = std::clamp(sample, -0x8000, 0x7FFF);

							dst.push_back(static_cast<int16_t>(sample));

							older = old;
							old = sample;
						}
					};

					for (int i = 0; i < 0x12; i++) {
						auto data = src;

						for (int blk = 0; blk < 4; blk++) {
							if (isStereo) { // ;left-samples (LO-nibbles), plus right-samples (HI-nibbles)
								decode_28_nibbles(data, blk, 0, left, oldLeft, olderLeft);
								decode_28_nibbles(data, blk, 1, right, oldRight, olderRight);
							} else { // ;first 28 samples (LO-nibbles), plus next 28 samples (HI-nibbles)
								decode_28_nibbles(data, blk, 0, left, oldLeft, olderLeft);
								decode_28_nibbles(data, blk, 1, left, oldLeft, olderLeft);
							}
						}

						src += 128;
					}

					if (isStereo) {
						for (size_t i = 0; i < left.size(); i++) {
							audioSamples.emplace_back(left[i], right[i]);
						}
					} else {
						for (size_t i = 0; i < left.size(); i++) {
							audioSamples.emplace_back(left[i], left[i]);
						}
					}
				//}
			}
		}
	}
}

void CDROM::queueCdAudioSector(const std::vector<uint8_t> &sector) {
	size_t offset = 0;

	static const uint8_t sync[12] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
	if (sector.size() >= 2352 && memcmp(sector.data(), sync, 12) == 0) {
		offset += 24;
	}

	const int32_t volL_L = static_cast<uint8_t>(cdLeftToLeft);
	const int32_t volL_R = static_cast<uint8_t>(cdLeftToRight);
	const int32_t volR_L = static_cast<uint8_t>(cdRightToLeft);
	const int32_t volR_R = static_cast<uint8_t>(cdRightToRight);

	for (size_t i = offset; i + 3 < sector.size(); i += 4) {
		int16_t left  = static_cast<int16_t>(sector[i]     | (sector[i + 1] << 8));
		int16_t right = static_cast<int16_t>(sector[i + 2] | (sector[i + 3] << 8));

		int32_t mixedLeft  = (static_cast<int32_t>(left) * volL_L + static_cast<int32_t>(right) * volR_L) >> 7;
		int32_t mixedRight = (static_cast<int32_t>(left) * volL_R + static_cast<int32_t>(right) * volR_R) >> 7;

		audioSamples.emplace_back(
		    static_cast<int16_t>(std::clamp<int32_t>(mixedLeft, -32768, 32767)),
		    static_cast<int16_t>(std::clamp<int32_t>(mixedRight, -32768, 32767))
		);
    }
}

void CDROM::applyPendingVolume() {
	cdLeftToLeft = pendingCdLeftToLeft;
	cdLeftToRight = pendingCdLeftToRight;
	cdRightToLeft = pendingCdRightToLeft;
	cdRightToRight = pendingCdRightToRight;
}

uint8_t CDROM::load(uint32_t addr) {
	//printf("CDROM Load %x\n", addr);
	//std::cerr << "";
	
	if(addr == 0) {
		uint8_t stat = 0;
		
		// 0-1 Index   Port 1F801801h-1F801803h index (0..3 = Index0..Index3)   (R/W)
		stat |= (_index) << 0;
		
		// 2 ADPBUSY XA-ADPCM fifo empty  (0=Empty) ;set when playing XA-ADPCM sound
		stat |= 0 << 2;
		
		// 3 PRMEMPT Parameter fifo empty (1=Empty) ;triggered before writing 1st byte
		stat |= (parameters.empty() ? 1 : 0) << 3;
		
		// 4 PRMWRDY Parameter fifo full  (0=Full)  ;triggered after writing 16 bytes	
		stat |= (parameters.size() >= 16 ? 0 : 1) << 4;
		
		// 5 RSLRRDY Response fifo empty  (0=Empty) ;triggered after reading LAST byte
		bool responseFifo = interrupts.is_empty() || interrupts.peek().responses.is_empty();
		stat |= (!responseFifo) << 5;
		//stat |= (responses.empty() ? 0 : 1) << 5;
		
		// 6 DRQSTS  Data fifo empty      (0=Empty) ;triggered after reading LAST byte
		stat |= (!isBufferEmpty) << 6;
		
		//7 BUSYSTS Command/parameter transmission busy  (1=Busy)
		stat |= (transmittingCommand) << 7;
		
		return stat;
	} else if(addr == 1) {
		uint8_t response = 0;// = responses.front();
		//responses.pop();

		/*if(!interrupts.is_empty()) {
			auto& in = interrupts.ref();

			if(!in.responses.is_empty()) {
				response = in.responses.get();
			}
		}*/
		
		if(!interrupts.is_empty()) {
			auto& in = interrupts.ref();

			if(!in.responses.is_empty()) {
				response = in.responses.get();

				if(in.responses.is_empty() && in.ack == true) {
					interrupts.get();
				}
			}
		}
		
		return response;
	} else if(addr == 2) {
		return readByte();
	} else if(addr == 3) {
		switch (_index) {
			/*case 0: return IE | 0xE0;
			case 2: return IE; // Mirrored
			
			case 1: return IF | 0xE0;
			case 3: return IF; // Mirrored*/
			case 0: case 2:
				return IE;
			case 1: case 3: {
				uint8_t res = 0b11100000;
				
				if(!interrupts.is_empty()) {
					auto p = interrupts.peek();
					
					/*if(p.fired) {
						res |= p._interrupt & 7;
					}*/
					if (interrupts.peek().delay <= 0) {
						res |= interrupts.peek()._interrupt & 7;
					}
				}
				
				return res;
			}
		}
		
		return 0;
	}
	
	return 0;
}

void CDROM::store(uint32_t addr, uint8_t val) {
	//printf("CDROM Store %x %x\n", addr, val);
	//std::cerr << "";
	
	if(addr == 0) {
		// https://psx-spx.consoledev.net/cdromdrive/#1f801800h-indexstatus-register-bit0-1-rw-bit2-7-read-only
		// READ-ONLY
		_index = static_cast<uint8_t>(val & 0x3);
		
		return;
	} else if(addr == 1) {
		switch (_index) {
			case 0: {
				// Command register
				decodeAndExecute(val & 0xFF);
				
				break;
			}
			
			case 3: {
				// 1F801801h.Index3 - Right-CD to Right-SPU Volume (W)
				pendingCdRightToRight = val;
				cdRightToRight = val;
				break;
			}

		default:
			printf("");
			break;
		}
	} else if(addr == 2) {
		switch (_index) {
			case 0: {
				parameters.push(val & 0xFF);

				break;
			}

			case 1: {
				IE = val;
				//triggerInterrupt();

				break;
			}

			case 2: {
				// 1F801802h.Index2 - Left-CD to Left-SPU Volume (W)
				pendingCdLeftToLeft = val;
				cdLeftToLeft = val;
				break;
			}

			case 3: {
				// 1F801802h.Index3 - Right-CD to Left-SPU Volume (W)
				pendingCdRightToLeft = val;
				cdRightToLeft = val;
				break;
			}

			default:
				printf("");
				break;
		}
	} else if(addr == 3) {
		switch (_index) {
			case 0: {
				// 1F801803h.Index0 - Request Register(W)
				//0 - 4 0    Not used(should be zero)
				//5   SMEN Want Command Start Interrupt on Next Command(0 = No change, 1 = Yes)
				//6   BFWR...
				//7   BFRD Want Data(0 = No / Reset Data Fifo, 1 = Yes / Load Data Fifo)// 1F801803h.Index0 - Request Register(W)
				//0 - 4 0    Not used(should be zero)
				//5   SMEN Want Command Start Interrupt on Next Command(0 = No change, 1 = Yes)
				//6   BFWR...
				//7   BFRD Want Data(0 = No / Reset Data Fifo, 1 = Yes / Load Data Fifo)
				if(val & 0x80) {
					// Load fifo
				    if(isEmpty()) {
						_sector.set(_readSector.read());
						isBufferEmpty = false;
					}
				} else {
					_sector.empty();
					 isBufferEmpty = true;
				}
				
				break;
			}
			
			case 1: {
			    //if (!interrupts.is_empty()) {
			    //    printf("Acknowledging interrupt %d\n", interrupts.peek()._interrupt);
			    //    interrupts.get();
			    //}

					/*if(!interrupts.is_empty() && interrupts.peek().fired) {
						interrupts.get();

						if(!interrupts.is_empty() && interrupts.peek().delay <= 0) {
							interrupts.ref().fired = true;

							if((IE & 7) & (interrupts.peek()._interrupt & 7)) {
								IRQ::trigger(IRQ::Interrupt::CDROM);
							}
						}
					}*/

				if (!interrupts.is_empty()) {
					interrupts.ref().ack = true;
					
					if (interrupts.ref().responses.is_empty()) {
						interrupts.get();
					} /*else {
						// TODO:
						if(interrupts.ref().attempts++ > 200) {
							interrupts.ref().responses.get();
							interrupts.ref().attempts = 0;
						}
					}*/
				}
				
				//IF &= ~(val & 0x1F);
				//triggerInterrupt();
				
				if(val & 0x40) {
					while(!parameters.empty())
						parameters.pop();
				}
				
				break;
			}
			
			case 2: {
				// 1F801803h.Index2 - Left-CD to Right-SPU Volume (W)
				pendingCdLeftToRight = val;
				cdLeftToRight = val;
				break;
			}

			case 3: {
				// 1F801803h.Index3 - Apply Volume Changes (bit5)
				if (val & 0x20) {
					applyPendingVolume();
				}

				break;
			}

		default:
			printf("");
			break;
		}
	} else {
		printf("");
	}
}

uint8_t CDROM::readByte() {
	if (_sector.isEmpty()) {
	    //printf("readByte: Sector is empty!\n");
		//assert(false);
		return 0;
	}
	
	// 0 - 0x800 - just data
	// 1 - 0x924 - whole sector without sync
	int dataStart = 12; //24 (12 sync, 4 header, 4 sub-header, 4 copy)
	if (!mode.sectorSize) dataStart += 12;
	
	/**
	* The PSX hardware allows to read 800h-byte or 924h-byte sectors,
	* indexed as [000h..7FFh] or [000h..923h], when trying to read further bytes,
	* then the PSX will repeat the byte at index [800h-8] or [924h-4] as padding value.
	*/
	if (!mode.sectorSize && _sector._pointer >= 0x800) {
		_sector._pointer++;
		return _sector.loadAt(dataStart + 0x800 - 8);
	}

	if (mode.sectorSize && _sector._pointer >= 0x924) {
		_sector._pointer++;
		return _sector.loadAt(dataStart + 0x924 - 4);
	}
	
	uint8_t data = _sector.loadAt(dataStart + _sector._pointer++);

	if(isEmpty()) {
		isBufferEmpty = true;
	}
	
	return data;
}

void CDROM::reset() {
	_index = 0;
	IE = 0;
	
	busyFor = 0;
	cycles = 0;
	
	seekLocation = 0;
	readLocation = 0;
	
	transmittingCommand = false;
	diskPresent = false;
	
	isBufferEmpty = false;
	mute = false;
	audioSamples.clear();
	
	pendingCdLeftToLeft = 0x7F;
	pendingCdLeftToRight = 0x00;
	pendingCdRightToLeft = 0x00;
	pendingCdRightToRight = 0x7F; // 0x80
	applyPendingVolume();
	
	_stats = {};
	mode = {};
	
	_disk = {};
	_readSector = {};
	_sector = {};
	
	while(!parameters.empty())
		parameters.pop();
	
	interrupts.clear();
}

void CDROM::swapDisk(const std::string& path) {
	_disk.set(path);
	
	diskPresent = true;
}

void CDROM::decodeAndExecute(uint8_t command) {
	//while(!interrupts.empty())
	//	interrupts.pop();
	
	//bool pending = !interrupts.is_empty();
	//assert(!pending);
	
	interrupts.clear();
	
	//printf("CMD: %x\n", command);
	//std::cerr << "\n";
	
	if(command == 0x01) {
		//   01h Getstat      -               INT3(stat)
		GetStat();
	} else if(command == 0x02) {
		// Setloc - Command 02h,amm,ass,asect --> INT3(stat)
		SetLoc();
	} else if (command == 0x04) {
		// Forward - Command 04h --> INT3(stat) --> optional INT1(report bytes)
		INT3();
		printf("CMD; Forward\n");
	}  else if(command == 0x06) {
		// ReadN - Command 06h --> INT3(stat) --> INT1(stat) --> datablock
		ReadN();
	} else if(command == 0x08) {
		// 0x08Stop - Command 08h --> INT3(stat) --> INT2(stat)
		Stop();
	} else if(command == 0x09) {
		// Pause - Command 09h --> INT3(stat) --> INT2(stat)
		Pause();
	} else if(command == 0x0E) {
		// Setmode - Command 0Eh,mode --> INT3(stat)
		SetMode();
	} else if(command == 0x0A) {
		// Init - Command 0Ah --> INT3(stat) --> INT2(stat)
		Init();
	} else if(command == 0x15) {
	 	// SeekL - Command 15h --> INT3(stat) --> INT2(stat)
		SeekL();
	} else if(command == 0x19) { 
		//   19h Test         sub_function    depends on sub_function (see below)
		decodeAndExecuteSub();
	} else if(command == 0x1A) {
		// GetID - Command 1Ah --> INT3(stat) --> INT2/5 (stat,flags,type,atip,"SCEx")
		GetID();
	} else if(command == 0x1B) {
		// ReadS - Command 1Bh --> INT3(stat) --> INT1(stat) --> datablock
		ReadS();
	} else if (command == 0x03) {
			// Too many args
			if (parameters.size() > 1) {
				INT(5);
				addResponse(0x01);
				addResponse(0x20);
				assert(false);
				return;
			}

			auto toBinary = [](uint8_t b) -> uint8_t {
				int hi = (b >> 4) & 0xF;
				int lo = b & 0xF;

				return hi * 10 + lo;
			};

			int trackNo = 0;
			if (parameters.size() == 1) {
				trackNo = toBinary(getParamater());
			}

			/*
			 * If no parameters where given, or it is a 0,
			 * then play starts at setloc position (if there was a pending unprocessed setloc)
			 * or otherwise starts at the current location (eg. the last point seeked, or the current location of the current song; if it was already playing)
			 */

			Location pos = Location::fromLBA(readLocation);

			// Start playing track n
			if (trackNo > 0) {
				int track = std::min(trackNo - 1, (int) _disk.tracks.size() - 1);
				pos = _disk.getTrackStart(track);
			} else {
				if (seekLocation != 0) {
					pos = Location::fromLBA(seekLocation); // Nice was readLocaiton :}
					seekLocation = 0;
				}

				//pos = _disk.getTrackStart(0);

				// TODO: Check audio status, if stop then ig pos = track(0).start?
			}

			readLocation = pos.toLba();
			_stats.setMode(Stats::Mode::Playing);

			INT(3);
			addResponse(_stats._reg);
	} else if (command == 0x11) {
		auto toBcd = [](uint8_t b) -> uint8_t {
			return ((b / 10) << 4) | (b % 10);
		};

		auto crc16 = [](const uint8_t* data, size_t length) -> uint16_t {
			uint16_t crc = 0;

			for (size_t i = 0; i < length; i++) {
				crc ^= (uint16_t) data[i] << 8;

				for (int bit = 0; bit < 8; bit++) {
					crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
				}
			}

			return crc;
		};

		int location = std::max(0, readLocation);
		int track = _disk.getTrackPosition(Location::fromLBA(readLocation - 1));;
		int trackStart = _disk.getTrackStart(track).toLba();

		uint8_t trackNo = 0xFF;
		int relativeLocation = 0;

		if (track >= 0) {
			trackNo = toBcd(track + 1);
			relativeLocation = location - trackStart;
		} else if (diskPresent && !_disk.tracks.empty() && location >= _disk.getSize().toLba()) {
			trackNo = 0xAA;
			relativeLocation = location - _disk.getSize().toLba();
		}

		auto relative = Location::fromLBA(relativeLocation);
		auto absolute = Location::fromLBA(location);

		bool audio = track >= 0 && _disk.isAudio(Location::fromLBA(location));
		uint8_t controlAdr = ((audio ? 0x0 : 0x4) << 4) | 0x1;

		uint8_t subQ[12] = {
			controlAdr,
			trackNo,
			toBcd(1),
			toBcd(relative.minutes),
			toBcd(relative.seconds),
			toBcd(relative.sectors),
			0,
			toBcd(absolute.minutes),
			toBcd(absolute.seconds),
			toBcd(absolute.sectors),
			0,
			0
		};

		uint16_t crc = ~crc16(subQ, 10);
		subQ[10] = crc >> 8;
		subQ[11] = crc & 0xFF;

		uint16_t stored = ((uint16_t) subQ[10] << 8) | subQ[11];
		bool valid = stored == (uint16_t) ~crc16(subQ, 10);
		assert(valid);

		INT(3, 1000);
		addResponse(subQ[1]); // track
		addResponse(subQ[2]); // index
		addResponse(subQ[3]); // minute (track)
		addResponse(subQ[4]); // second (track)
		addResponse(subQ[5]); // sector (track)
		addResponse(subQ[7]); // minute (disc)
		addResponse(subQ[8]); // second (disc)
		addResponse(subQ[9]); // sector (disc)
	} else if (command == 0x13) {
		auto toBcd = [](uint8_t b) -> uint8_t {
			return ((b / 10) << 4) | (b % 10);
		};
		
		INT3();
		addResponse(toBcd(0x01));
		addResponse(toBcd(_disk.tracks.size()));
	} else if (command == 0x14) {
		// GetTD - Command 14h,track --> INT3(stat,mm,ss) ;BCD
		auto toBinary = [](uint8_t b) -> uint8_t {
			int hi = (b >> 4) & 0xF;
			int lo = b & 0xF;
			
			return hi * 10 + lo;
		};
		
		auto toBcd = [](uint8_t b) -> uint8_t {
			return ((b / 10) << 4) | (b % 10);
		};
		
		auto track = toBinary(getParamater());
		
		if (track == 0) {
			// end of last track
			auto diskSize = _disk.getSize();
			
			INT(3);
			addResponse(_stats._reg);
			addResponse(toBcd(diskSize.minutes));
			addResponse(toBcd(diskSize.seconds));
		} else if (track <= _disk.tracks.size()) {
			// Start of n track
			auto start = _disk.getTrackStart(track - 1);
			
			INT(3);
			addResponse(_stats._reg);
			addResponse(toBcd(start.minutes));
			addResponse(toBcd(start.seconds));
		} else {
			// error
			INT(5);
			addResponse(0x10);
			
			return;
		}
	} else if (command == 0x16) {
		// SeekP - Command 16h --> INT3(stat) --> INT2(stat)
		readLocation = seekLocation;
		seekLocation = 0;
		
		auto prevStat = _stats._reg;
		
		_stats.setMode(Stats::Mode::None);
		INT3();
		
		_stats._reg = prevStat;
		INT(2, 500000);
		addResponse(_stats._reg);
		_stats.setMode(Stats::Mode::None);
	} else if (command == 0x1E) {
		// ReadTOC - Command 1Eh --> INT3(stat) --> INT2(stat)
		INT3();
		INT2();
	} else if (command == 0x0B) {
		// Mute - Command 0Bh --> INT3(stat)
		mute = true;
		INT3();
	} else if (command == 0x0C) {
		// Demute - Command 0Ch --> INT3(stat)
		mute = false;
		INT3();
	} else if (command == 0x0D) {
		// Setfilter - Command 0Dh,file,channel --> INT3(stat)
		filter = {getParamater(), getParamater()};

		INT3();
	}  else if (command == 0x50 || command == 0x51 || command == 0x52 || command == 0x53 || command == 0x54 ||
	           command == 0x55 || command == 0x56 || command == 0x57) {
		INT(5);
		addResponse(0x13);
		addResponse(0x40);
		printf("any?\n");
	} else {
		INT3();
		printf("Unhandled command %x\n", command);
	}
	
	/**
	 * 1. Command busy flag set immediately.
     * 2. Response FIFO is populated.
     * 3. Command is being processed.
     * 4. Command busy flag is unset and parameter fifo is cleared.
     * 5. Shortly after (around 1000-6000 cycles later), CDROM IRQ is fired.
     * 
     * Not sure how to calculate the correct cycles count?
	 */
	busyFor = 1000;
	transmittingCommand = true;
	
	while(!parameters.empty())
		parameters.pop();
}

void CDROM::decodeAndExecuteSub() {
	if(parameters.empty()) {
		printf("NAWWWWWWWWWWWWW\n");
		return;
	}
	
	uint8_t command = parameters.front();
	parameters.pop();
	
	//printf("Sup command: %x\n", command);
	
	if (command == 0x04) {
		//   04h      -   INT3(stat)         ;Start SCEx reading and reset counters
		_stats.motor = 1;
		scexCounter = 0;

		INT3();
		
		if (readLocation < 1024) {
			scexCounter++;
		}
	} else if (command == 0x05) {
		//   05h      -   INT3(total,success);Stop SCEx reading and get counters
		INT(3);
		addResponse(scexCounter);
		addResponse(scexCounter);
	} else if(command == 0x20) {
		//   20h      -   INT3(yy,mm,dd,ver) ;Get cdrom BIOS date/version (yy,mm,dd,ver)
		
		//interrupts.push(3);
		INT(3);
		
		// 95h,05h,16h,C1h  ;PSX (LATE-PU-8)          16 May 1995, version vC1 (a)
		addResponse(0x95);
		addResponse(0x05);
		addResponse(0x16);
		addResponse(0xC1);
	} else if(command >= 0x60 && command <= 0xFF) {
		INT(5);
		addResponse(0x11);
		addResponse(0x40);
	} else {
		printf("CDROM; Unknown subcommand\n");
		assert(false);
	}
}

bool CDROM::isEmpty() {
	if(_sector.isEmpty())
		return true;
	
	if(!mode.sectorSize && _sector._pointer >= 0x800) return true;
	if( mode.sectorSize && _sector._pointer >= 0x924) return true;
	
	return false;
}

void CDROM::GetStat() {
	/**
	 * 7  Play          Playing CD-DA         ;\only ONE of these bits can be set
	 * 6  Seek          Seeking               ; at a time (ie. Read/Play won't get
	 * 5  Read          Reading data sectors  ;/set until after Seek completion)
	 * 4  ShellOpen     Once shell open (0=Closed, 1=Is/was Open)
	 * 3  IdError       (0=Okay, 1=GetID denied) (also set when Setmode.Bit4=1)
	 * 2  SeekError     (0=Okay, 1=Seek error)     (followed by Error Byte)
	 * 1  Spindle Motor (0=Motor off, or in spin-up phase, 1=Motor on)
	 * 0  Error         Invalid Command/parameters (followed by Error Byte)
	 */
	
	_stats.shellOpen = !diskPresent;
	_stats.motor     = 1;
	
	INT(3);
	addResponse(_stats._reg);
}

void CDROM::SetLoc() {
	// Setloc - Command 02h,amm,ass,asect --> INT3(stat)
	
	/*
	 * Sets the seek target - but without yet starting the seek operation.
	 * 
	 * amm:    minute number on entire disk (00h and up)
	 * ass:    second number on entire disk (00h to 59h)
	 * asect:  sector number on entire disk (00h to 74h)
	 */
	
	uint8_t amm   = parameters.front(); parameters.pop();
	uint8_t ass   = parameters.front(); parameters.pop();
	uint8_t asect = parameters.front(); parameters.pop();
	
	auto toBinary = [](uint8_t bcd) -> uint8_t {
		return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
	};
	
	// Convert BCD to binary
	uint8_t minutes = toBinary(amm);
	uint8_t seconds = toBinary(ass);
	uint8_t sectors = toBinary(asect);
	
	seekLocation = sectors + (seconds * 75) + (minutes * 60 * 75);
	
	/**
	 * E = Error 80h appears on some commands (02h..09h, 0Bh..0Dh, 10h..16h, 1Ah, 1Bh?, and 1Dh)
	 * when the disk is missing, or when the drive unit is disconnected from the mainboard.
	 */
	if(!diskPresent) {
		INT(5);
		addResponse(0x11);
		addResponse(0x80);
		
		return;
	}
	
	INT(3, 5000);
	addResponse(_stats._reg);
}

void CDROM::ReadN() {
	// ReadN - Command 06h --> INT3(stat) --> INT1(stat) --> datablock
	
	if(!diskPresent) {
		INT(5);
		addResponse(0x11);
		addResponse(0x80);
		
		assert(false);
		
		return;
	}

	readLocation = seekLocation;
	_stats.setMode(Stats::Mode::Reading);
	
	INT(3, 1000);
	addResponse(_stats._reg);

    /*Location pos = Location::fromLBA(readLocation);
    auto rawSector = _disk.read(pos);
    _readSector.set(rawSector);

	// Read data after 30ms
	INT(1, 30000);
	addResponse(_stats._reg);

    readLocation++;*/
}

void CDROM::Stop() {
	// Stop - Command 08h --> INT3(stat) --> INT2(stat)
	INT3();
	
	// Stops motor with magnetic brakes
	// moves the drive head to the beginning of the first track.
	_stats.setMode(Stats::Mode::None);

	// TODO; Stop audio
	audioSamples.clear();
	_stats.motor = 0;
	
	INT2();
}

void CDROM::Pause() {
	INT(3, 5000);
	addResponse(_stats._reg);
	
	_stats.setMode(Stats::Mode::None);
	//audioSamples.clear();
	//_stats.motor = 1;
	
	INT(2, 20000);
	addResponse(_stats._reg);
}

void CDROM::SetMode() {
	// Setmode - Command 0Eh,mode --> INT3(stat)
	/**
	 * 7   Speed       (0=Normal speed, 1=Double speed)
     * 6   XA-ADPCM    (0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input)
     * 5   Sector Size (0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes)
     * 4   Ignore Bit  (0=Normal, 1=Ignore Sector Size and Setloc position)
     * 3   XA-Filter   (0=Off, 1=Process only XA-ADPCM sectors that match Setfilter)
     * 2   Report      (0=Off, 1=Enable Report-Interrupts for Audio Play)
     * 1   AutoPause   (0=Off, 1=Auto Pause upon End of Track) ;for Audio Play
     * 0   CDDA        (0=Off, 1=Allow to Read CD-DA Sectors; ignore missing EDC)
	 */
	auto parm = getParamater();
    //mode._reg = parm;

	mode = Mode(parm);
	
	INT(3, 2000);
	addResponse(_stats._reg);
}

void CDROM::Init() {
	// Init - Command 0Ah --> INT3(stat) --> INT2(stat)

	/**
	 * Timings for most other commands should be similar as above.
	 * One exception is the Init command,
	 * which is doing some initialization before sending the 1st response:
	 * 
	 * Init                   0013cceh  000f820h..00xxxxxh
	 */
	INT(3, 0x13CE);
	addResponse(_stats._reg);
	
	_stats.setMode(Stats::Mode::None);
	
	mode._reg = 0;
	
	INT2();
	addResponse(_stats._reg);
}

void CDROM::SeekL() {
	// TODO; Handle this
	
	// SeekL - Command 15h --> INT3(stat) --> INT2(stat)
	
	// TODO; This command will stop any current or pending ReadN or ReadS.
	// TODO; fter the seek, the disk stays on the seeked location forever,
	// (namely: when seeking sector N, it does stay at around N-8..N-0 in single speed mode,
	// or at around N-5..N+2 in double speed mode).
	
	// TODO; Seek to Setloc's location in data mode

	readLocation = seekLocation;
	
	/**
	 * E = Error 80h appears on some commands (02h..09h, 0Bh..0Dh, 10h..16h, 1Ah, 1Bh?, and 1Dh)
	 * when the disk is missing, or when the drive unit is disconnected from the mainboard.
	 */
	if(!diskPresent) {
		INT(5);
		addResponse(0x11);
		addResponse(0x80);
		
		return;
	}
	
	INT(3, 5000);
	addResponse(_stats._reg);
	
	// TODO; This gets pushed once seek is completed
	INT(2, 500000);
	addResponse(_stats._reg);
	
	_stats.setMode(Stats::Mode::None);
}

void CDROM::GetID() {
	// GetID - Command 1Ah --> INT3(stat) --> INT2/5 (stat,flags,type,atip,"SCEx")
	
	/*
	* Drive Status           1st Response   2nd Response
    * Door Open              INT5(11h,80h)  N/A
    * Spin-up                INT5(01h,80h)  N/A
    * Detect busy            INT5(03h,80h)  N/A
    * No Disk                INT3(stat)     INT5(08h,40h, 00h,00h, 00h,00h,00h,00h)
    * Audio Disk             INT3(stat)     INT5(0Ah,90h, 00h,00h, 00h,00h,00h,00h)
    * Unlicensed:Mode1       INT3(stat)     INT5(0Ah,80h, 00h,00h, 00h,00h,00h,00h)
    * Unlicensed:Mode2       INT3(stat)     INT5(0Ah,80h, 20h,00h, 00h,00h,00h,00h)
    * Unlicensed:Mode2+Audio INT3(stat)     INT5(0Ah,90h, 20h,00h, 00h,00h,00h,00h)
    * Debug/Yaroze:Mode2     INT3(stat)     INT2(02h,00h, 20h,00h, 20h,20h,20h,20h)
    * Licensed:Mode2         INT3(stat)     INT2(02h,00h, 20h,00h, 53h,43h,45h,4xh)
    * Modchip:Audio/Mode1    INT3(stat)     INT2(02h,00h, 00h,00h, 53h,43h,45h,4xh)
	*/
	
	if (_stats.getShell()) {
		INT(5);
		addResponse(0x11);
		addResponse(0x80);
		
		return;
	}
	
	INT(3, 0x4A00);
	addResponse(_stats._reg);

	if (!diskPresent || _disk.tracks.empty()) {
		_stats.idError = 1;

		INT(5, 449000);
		addResponse(_stats._reg);
		addResponse(0x40);

		for (int i = 0; i < 6; i++)
			addResponse(0x00);

		return;
	}

	if (_disk.isAudio(Location(0, 2, 0))) {
		INT(5, 449000);
		addResponse(0x0A);
		addResponse(0x90);

		for (int i = 0; i < 6; i++)
			addResponse(0x00);

		return;
	}

	INT(2, 449000);
	addResponse(0x02);
	addResponse(0x00);
	addResponse(0x20);
	addResponse(0x00);

	addResponse('S');
	addResponse('C');
	addResponse('E');
	addResponse('A');

	/*if (diskPresent) {
		INT(2, 449000);
	} else {
		INT(5);
		// TODO;
		assert(false);
	}

	// Licensed:Mode2         INT3(stat)     INT2(02h,00h, 20h,00h, 53h,43h,45h,4xh)
	addResponse(0x02); // Stat
	addResponse(0x00); // Flags
	addResponse(0x20); // Type
	addResponse(0x00); // Atip (Always zero)

	addResponse('S'); // 'S' 0x53
	addResponse('C'); // 'C' 0x43
	addResponse('E'); // 'E' 0x45
	addResponse('A'); // // Region code (A=USA, E=Europe, I=Japan)*/
	
	/*if (diskPresent) {
		//INT(2, 449000);
	    INT(2, 200); // random ass number

	    addResponse(0x02);
	    addResponse(0x00);
	    addResponse(0x20);
	    addResponse(0x00);
	    addResponse('S');
	    addResponse('C');
	    addResponse('E');
	    addResponse('A');
	} else {
	    // No Disk                INT3(stat)     INT5(08h,40h, 00h,00h, 00h,00h,00h,00h)
		INT(5, 200);

		addResponse(0x08);
		addResponse(0x40);
		addResponse(0x00);
		addResponse(0x00);
		addResponse(0x00);
		addResponse(0x00);
		addResponse(0x00);
		addResponse(0x00);

		return;
	}*/
	
	// Licensed:Mode2         INT3(stat)     INT2(02h,00h, 20h,00h, 53h,43h,45h,4xh)
	/*addResponse(0x02); // Stat
	addResponse(0x00); // Flags
	addResponse(0x20); // Type
	addResponse(0x00); // Atip (Always zero)
	
	addResponse('S'); // 'S' 0x53
	addResponse('C'); // 'C' 0x43
	addResponse('E'); // 'E' 0x45
	addResponse('A'); // // Region code (A=USA, E=Europe, I=Japan)*/
}

void CDROM::ReadS() {
	readLocation = seekLocation;

	audioSamples.clear();
	_stats.setMode(Stats::Mode::Reading);
	
	INT(3, 500);
	addResponse(_stats._reg);
}

void CDROM::INT2() {
	INT(2);
	addResponse(_stats._reg);
}

void CDROM::INT3() {
	INT(3);
	addResponse(_stats._reg);
}

uint8_t CDROM::getParamater() {
	auto parm = parameters.front();
	parameters.pop();
	
	return parm;
}
