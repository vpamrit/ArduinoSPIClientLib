#include <SPIHelpers.h>

uint8_t computeHeaderChecksum(const SPIPacketHeader &header)
{
    uint8_t checksum = 0;

    checksum ^= header.packetPos;
    checksum ^= header.numPackets;
    checksum ^= header.structType;
    checksum ^= header.length;
    checksum ^= header.request;

    return checksum;
}

bool verifyHeaderChecksum(const SPIPacketHeader &header)
{
    return computeHeaderChecksum(header) == header.headerChecksum;
}

bool verifyDataChecksum(volatile char* data, const SPIPacketHeader &header)
{
    if (header.length > NUM_BYTES_DATA)
    {
        return false;
    }

    uint8_t checksumData = computeDataChecksum(data, header.length);
    return checksumData == header.checksum;
}

uint8_t computeDataChecksum(volatile char* data, uint8_t packetSize)
{
    uint8_t checksum;

    for (int i = 0; i < packetSize; ++i)
    {
        checksum ^= data[i];
    }

    return checksum;
};