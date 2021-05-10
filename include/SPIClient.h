#include <Arduino.h>
#include <SPI.h>
#include "MessageQueue.h"

#ifndef SPI_CLIENT_H
#define SPI_CLIENT_H

#define SET_MODE_SWITCH(var) \
    modeSwitch.set();        \
    mode = var;
#define SET_MODE(var) \
    mode = var;
#define IS_MODE(var) mode == var

struct ModeSwitch
{
    bool state = true;

    bool set();
    bool use(bool consume = true);
};

;

class SPIClient
{
public:
    static const uint32_t FLUSH_BYTES;

    enum CLIENT_STATE
    {
        FLUSHING,
        STANDARD,
        START_UP
    };

    virtual uint8_t
    operate() = 0;

    bool accept(uint8_t steuctType, char *buffer, uint16_t length, bool forcedTransmit);
    Metadata readMessage(char *buffer);

protected:
    static MessageToBeReceived msgComingIn;
    static MessageToBeSent msgGoingOut;

    static volatile bool full;
    static volatile CLIENT_STATE mode;
    static ModeSwitch modeSwitch;

    static uint8_t handleFlush(char c);
    static uint8_t performFlushGet();

    static uint8_t performStartUpGet(bool consumeStateFreshness);

    static void performStartUpUpdate(char c, bool consume);
    static uint8_t performStandardGet();

    static void
    performStandardUpdate(char c, SPIPacketHeaderUnion &header, char *dataBuf);
};

class SPIMaster : public SPIClient
{
public:
    uint8_t operate() override;
};

class SPISlave : public SPIClient
{
public:
    uint8_t operate() override;
};

/* 
 * 2 Models
 * WRONG MODEL: 
 * 1 Model (SPI Transfer Byte ---> Update Slave ---> Slave Gives Instant Feedback)
 * Master: Write desires, then Update Flush / Update State
 * Slave: Write desires, then Update Flush / Update State
 * 
 * RIGHT MODEL:
 * Model 2: (SPI Transfer Byte --> Update Slave --> Slave Gives Old Feedback) MORE LIKELY
 * Master: Read / Update State THEN Write
 * Slave: WRITE immediately THEN Update State / Handle Flush 
 */

#endif //SPI_HELPERS_H