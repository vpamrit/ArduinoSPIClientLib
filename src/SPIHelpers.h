
#pragma once

#include <Arduino.h>

#ifndef SPI_HELPERS_H
#define SPI_HELPERS_H

// flush codes
#define FLUSH_NOOP 0x00
#define FLUSH_NEW_TRANSMISSION 0x02
#define FLUSH_CRITICAL_ERROR 0x03

//general read sequence responses
#define SUCCESS_BYTE 0x11
#define BUSY_BYTE 0x22
#define FLUSH_REQUEST_BYTE 0xFF

#define NUM_BYTES_DATA 32
#define NUM_BYTES_HEADER sizeof(SPIPacketHeader)
#define NUM_BYTES_PER_PACKET (NUM_BYTES_HEADER + NUM_BYTES_DATA)


//SPIPacketHeaderTypes
#define START_UP_STRUCT 0xFF
#define NEW_ALERT 0x11
#define REGISTER_PRINT 0x22
#define DELETE_PERSON 0x33
#define BLANK_PACKET 0x44
#define TEST_STRUCT 0x55

// request piggyback types
#define FORCE_NEW_TRANSMIT 0x01
#define RETRY_TRANSMISSION 0x02
#define TRANSMIT_COMPLETE 0x04
#define PACKET_COMPLETE 0x08
#define UNAVAILABLE 0x10
#define NONE 0x20


// mini config
struct SPIConfig
{
    static const bool isMaster = true;
};

/*** class ***/
// class SPIDeSerializer
// {
//     virtual Packet getObject(char *obj);
// };

/*
* Forward declarations
*/
class SPIPacketHeader;
uint8_t computeHeaderChecksum(const SPIPacketHeader &header);
bool verifyHeaderChecksum(const SPIPacketHeader &header);
bool verifyDataChecksum(volatile char* data, const SPIPacketHeader &header);
uint8_t computeDataChecksum(volatile char* data, uint8_t packetSize);

/*** class ***/
class SPIInterface
{
public:
    virtual unsigned char *serialize();
    virtual unsigned char *deserialize();
    virtual unsigned char *getByteArray();
    virtual uint32_t getSize();
    uint32_t size;
};

// 12 byte header, 32 byte body
struct SPIPacketHeader
{
    uint8_t structType;
    uint8_t length;  //can be zero for signal packets
    uint8_t request; // some message
    uint16_t expectedNextPacketPos;
    uint16_t packetPos;
    uint16_t numPackets;
    uint8_t checksum;
    uint8_t id;
    uint8_t headerChecksum;

    SPIPacketHeader() = default;
    SPIPacketHeader(uint8_t structType, uint8_t request) : structType(structType), length(0),
                                                           request(request), packetPos(0), numPackets(1), checksum(0), id(0)
    {
        headerChecksum = computeHeaderChecksum(*this);
    }

    bool operator==(volatile const SPIPacketHeader &other) const
    {
        return other.id == this->id && this->numPackets == other.numPackets;
    }
};

typedef union
{
    SPIPacketHeader header;
    unsigned char buffer[sizeof(SPIPacketHeader)];
} SPIPacketHeaderUnion;



struct ConsumableByte
{
    uint8_t value;
    bool consumed = true;

    void set(uint8_t val)
    {
        value = val;
        consumed = false;
    };

    bool unconsumed()
    {
        return !consumed;
    }

    uint8_t check()
    {
        return value;
    }

    uint8_t consume(bool yes = true)
    {
        uint8_t val = consumed ? NONE : val;
        consumed = yes;
        return val;
    }
};

#endif // SPI_HELPERS_H