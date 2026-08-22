#pragma once

#include <stdint.h>
#include <deque>
#include <queue>
#include <utility>
#include <glm/ext/scalar_uint_sized.hpp>

#include "Disk.h"
#include "fifo.h"

#include "../IRQ.h"

class CDROM {
	union Stats {
		enum class Mode { None, Reading, Seeking, Playing };
		
		struct {
			uint8_t error : 1;
			uint8_t motor : 1;
			uint8_t seekError : 1;
			uint8_t idError : 1;
			uint8_t shellOpen : 1;
			uint8_t read : 1;
			uint8_t seek : 1;
			uint8_t play : 1;
		};
		
		uint8_t _reg;
		
		void setMode(Mode mode) {
			error = seekError = idError = false;
			read = seek = play = false;
			motor = true;
			
			if (mode == Mode::Reading) {
				read = true;
			} else if (mode == Mode::Seeking) {
				seek = true;
			} else if (mode == Mode::Playing) {
				play = true;
			}
		}
		
		void setShell(bool opened) {
			shellOpen = opened;
			
			setMode(Mode::None);
			
			if (opened) {
				motor = false;
			}
		}
		
		bool getShell() const { return shellOpen; }
		
		Stats() : _reg(0) {}
	};
	
	union Mode {
		struct {
			uint8_t cdda       : 1; // (0=Off, 1=Allow to Read CD-DA Sectors; ignore missing EDC)
			uint8_t autoPause  : 1; // (0=Off, 1=Auto Pause upon End of Track) ;for Audio Play
			uint8_t report     : 1; // (0=Off, 1=Enable Report-Interrupts for Audio Play)
			uint8_t xaFilter   : 1; // (0=Off, 1=Process only XA-ADPCM sectors that match Setfilter)
			uint8_t ignoreBit  : 1; // (0=Normal, 1=Ignore Sector Size and Setloc position)
			uint8_t sectorSize : 1; // (0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes)
			uint8_t xaAdpcm    : 1; // (0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input)
			uint8_t speed      : 1; // (0=Normal speed, 1=Double speed)
		};
		
		uint8_t _reg;
		
		Mode() : _reg(0) {}
		Mode(uint8_t reg) : _reg(reg) {}
	};
	
	struct Interrupt {
		uint8_t _interrupt;
		fifo<uint8_t, 16> responses;
		
		int32_t delay = 0;
		int32_t attempts = 0;
		bool ack = false;
		
		Interrupt() = default;
		
		Interrupt(uint8_t interrupt, int32_t delay)
			: _interrupt(interrupt), delay(delay) {
			
		}
	};
	
public:
	CDROM();
	
	void step(uint32_t cycles);
	uint32_t cyclesUntilNextInterrupt() const;

	void handleSector();
	bool popAudioSample(int16_t& left, int16_t& right);
	
	uint8_t load(uint32_t addr);
	void store(uint32_t addr, uint8_t val);
	
	uint8_t readByte();

	void reset();
	
public:
	void swapDisk(const std::string& path);
	
	void decodeAndExecute(uint8_t command);
	void decodeAndExecuteSub();

private:
	bool isEmpty();	
	void queueCdAudioSector(const std::vector<uint8_t>& sector);
	void applyPendingVolume();
	
private:
	// CDROM Commands
	void GetStat();
	void SetLoc();
	void ReadN();
	void Stop();
	void Pause();
	void SetMode();
	void Init();
	void SeekL();
	void GetID();
	void ReadS();
	
private:
	/*void triggerInterrupt() {
		if((IE & IF) != 0) {
			// Interrupt
			IRQ::trigger(IRQ::Interrupt::CDROM);
		}
	}*/
	
	// Interrupts
	void INT2();
	void INT3();
	
	void INT(uint8_t in, int32_t delay = 50000) {
		interrupts.add(Interrupt(in, delay));
		//interrupts.push(in);
	}
	
	void addResponse(uint8_t response) {
		//responses.push(response);
		if(interrupts.is_empty()) {
			printf("");
			return;
		}
		
		auto& entry = interrupts.ref(interrupts.size() - 1);
		if(entry.responses.is_full()) {
			return;
		}
		
		entry.responses.add(response);
	}
	
private:
	uint8_t getParamater();
	
private:
	uint8_t _index = 0;
	
	uint8_t IE = 0;
	//int8_t IF = 0;
	
	int32_t busyFor = 0;
	uint32_t cycles = 0;
	
	uint32_t scexCounter = 0;
	
private:
	/**
	 * After copying a bunch of shit from Avocado because it's been weeks...
	 * THIS WAS THE ISSUE. I am literally out of words... ;-;
	 */
	int seekLocation;
	int readLocation;
	
	bool transmittingCommand = false;
	bool diskPresent = false;
	
	bool isBufferEmpty = true;
	
	bool mute = false;
	std::deque<std::pair<int16_t, int16_t>> audioSamples;
	
	uint8_t pendingCdLeftToLeft = 0x80;
	uint8_t pendingCdLeftToRight = 0x00;
	uint8_t pendingCdRightToLeft = 0x00;
	uint8_t pendingCdRightToRight = 0x80;
	
	uint8_t cdLeftToLeft = 0x80;
	uint8_t cdLeftToRight = 0x00;
	uint8_t cdRightToLeft = 0x00;
	uint8_t cdRightToRight = 0x80;
	
private:
	Stats _stats;
	Mode mode;

private:
	Disk _disk;
	Sector _readSector;
	Sector _sector;
	
private:
	// TODO; Make size of 16
	// Rename to parameters
	std::queue<uint8_t> parameters;
	//std::queue<uint8_t> responses;
	//std::queue<uint8_t> interrupts;
	
	//std::queue<Interrupt> interrupts;
	fifo<Interrupt, 16> interrupts;
};
