#include <Arduino.h>
#include "SPIHelpers.h"

#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

struct ReadTransmissionState
{
    uint8_t outgoingRequest;
    uint8_t incomingRequest;
    uint16_t weExpectToReceiveFromThem;
    uint16_t theyExpectToReceiveFromUs;
};

struct Metadata {
    int16_t checksum;
    int16_t size;
};

class MessageToBeSent
{

    enum State
    {
        HEADER,
        BODY,
        BLANK_BODY
    };

    static volatile char out[500];
    static volatile uint32_t size;
    static volatile uint16_t id;

    // variables to expose internal state
    static volatile bool full;
    static volatile bool ready;
    static volatile bool isMaster; //TODO: init this somehow???
    static volatile uint8_t packetNum;

    //outgoing piggyback
    static volatile ConsumableByte piggyback;
    static volatile uint16_t nextPacketPos;           // this is theirs
    static volatile uint16_t ourExpectedNextPacket;   // this is ours
    static volatile uint16_t theirExpectedNextPacket; // this is ours

    // internal state logic
    static volatile uint32_t dataByteNum;
    static volatile uint8_t headerByteNum;
    static volatile State msgMode;



    static int getHeaderByte();
    static void initNextHeader();

public:
    static volatile uint16_t writeRetries;
    /*
    * TODO: Make these volatile. However, volatility for all these variables is probably unnecessary.
    * Even for the slave, the context of execution is always within a single interrupt. Therefore,
    * all compiler optimization with assumptions of a single thread should be justified
    */
    static SPIPacketHeader nextHeader;
    static SPIPacketHeaderUnion currentHeader;

    static bool acceptTransmission(uint8_t structType, char *dataBuffer, uint32_t length, bool force = false);

    static uint8_t getCurrentByte();

    /*
    *   always called AFTER getCurrentByte!!!! so safe to ++byteNum
    *   this updates the state, it should have been seeded by read ahead of time
    *   so stuff like expectedNextPacketPos and theirexpectedNextPos and outgoingPiggyback
    *   all of this should have been seeded!!!
    */

    //TODO: ISSUE WITH INIT, BECAUSE WE DON'T INIT NEXT HEADER INITIALLY
    static void updateState(ReadTransmissionState *state);

    //TODO: work on this thing, when first call don't increment packetNum?!
    static void initNextHeader(ReadTransmissionState *state);

    /*
    *
    * Simply resets the transaction
    * 
    */
    static void reset();

    /*
    * Reset header ID, clear full
    * Set to struct type to blank
    * We can reset this stuff after seeding
    */
    static void refresh();
};

class MessageToBeReceived
{
public:
    static volatile bool full;
    static volatile bool ready;
    static volatile uint32_t readRetries;

private:
    static volatile char in[500];       // two bytes
    static volatile bool isMaster; //TODO: not sure if this is guaranteed order for static init
    static volatile uint8_t structType;
    static volatile uint32_t size;
    static volatile uint32_t weExpectToReceiveFromThem;
    static volatile int32_t theyExpectToReceiveFromUs;
    static volatile uint32_t lastReceivedPacket;
    static volatile uint16_t id;
    static volatile int16_t finalDataChecksum;

public:
    static Metadata readMessage(char* buf); 

    static ReadTransmissionState writePacket(SPIPacketHeader &header, char *dataBuffer);

    static bool consume();
    static void reset(); 
    static void refresh();
};

#endif // MESSAGE_QUEUE_H