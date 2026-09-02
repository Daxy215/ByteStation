#include "DigitalController.h"
#include <iostream>

#include "../SIO.h"

DigitalController::DigitalController(uint8_t index) : INDEX(index), input(0) {
}

void DigitalController::step(uint32_t cycles) {

}

uint16_t DigitalController::load(uint32_t val) {
    /**
     ** Send Reply Comment
     ** 01h Hi-Z Controller address
     ** 42h idlo Receive ID bit0..7 (variable) and Send Read Command (ASCII "B")
     ** TAP idhi Receive ID bit8..15 (usually/always 5Ah)
     ** MOT swlo Receive Digital Switches bit0..7
     ** MOT swhi Receive Digital Switches bit8..15
     ** --- transfer stops here for digital pad (or analog pad in digital mode) ---
     ** 00h adc0 Receive Analog Input 0 (if any) (eg. analog joypad or mouse)
     ** 00h adc1 Receive Analog Input 1 (if any) (eg. analog joypad or mouse)
     ** --- transfer stops here for analog mouse ----------------------------------
     ** 00h adc2 Receive Analog Input 2 (if any) (eg. analog joypad)
     ** 00h adc3 Receive Analog Input 3 (if any) (eg. analog joypad)
     ** --- transfer stops here for analog pad (in analog mode) -------------------
     ** --- transfer stops here for nonstandard devices (steering/twist/paddle) ---
     * 
     */
    
    // TODO; https://psx-spx.consoledev.net/controllersandmemorycards/#controller-and-memory-card-multitap-adaptor
    
    switch (mode) {
        case Idle: {
            if (val == 0x01) {
                mode = Connected;
                interrupt = true;
                
                return 0xFF;
            }
            
            // Clear data buffer
            std::queue<uint8_t> empty;
            std::swap(data, empty);
            
            interrupt = false;
            
            return 0xFF;
        }
        
        case Connected: {
            mode = Transferring;
            
            if (val == 0x42) {
                interrupt = true;
                
                uint8_t b0 = TYPE & 0xFF;
                uint8_t b1 = (TYPE >> 8) & 0xFF;
                uint8_t b2 = (~input._byte[0]) & 0xFF;
                uint8_t b3 = (~input._byte[1]) & 0xFF;
                
                while (!data.empty()) data.pop();
                
                data.push(b0);
                data.push(b1);
                data.push(b2);
                data.push(b3);
                
                return getNextByte();
            }
            
            //mode = Idle;
            
            // Clear data buffer
            //std::queue<uint8_t> empty;
            //std::swap(data, empty);
            //interrupt = false;
            
            return getNextByte();
        }
        
        case Transferring: { return getNextByte(); }
    }
}

void DigitalController::reset() {
    mode = Idle;
    interrupt = false;
}

uint8_t DigitalController::getNextByte() {
    uint8_t d = 0xFF;
    
    if (!data.empty()) {
        d = data.front();
        data.pop();
    }
    
    interrupt = !data.empty();
    if (!interrupt) mode = Idle;
    
    return d;
}
