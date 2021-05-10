#include <SPIClient.h>

static uint8_t PACKET_RETRY_LIMIT = 5;

bool ModeSwitch::set()
{
    state = true;
    return state;
}

bool ModeSwitch::use(bool consume)
{
    if (!consume)
        return state;

    bool ret = state;
    state = false;
    return ret;
}

MessageToBeReceived SPIClient::msgComingIn;
MessageToBeSent SPIClient::msgGoingOut;
volatile bool SPIClient::full = false;
volatile SPIClient::CLIENT_STATE SPIClient::mode = SPIClient::CLIENT_STATE::FLUSHING;
ModeSwitch SPIClient::modeSwitch;
const uint32_t SPIClient::FLUSH_BYTES = sizeof(SPIPacketHeader) + NUM_BYTES_PER_PACKET * 2;

bool SPIClient::accept(uint8_t structType, char *buffer, uint16_t length, bool forcedTransmit)
{
    return msgGoingOut.acceptTransmission(structType, buffer, length, forcedTransmit);
}

Metadata SPIClient::readMessage(char *buffer)
{
    return msgComingIn.readMessage(buffer);
}

uint8_t
SPIClient::handleFlush(char c)
{
    static uint32_t nullSequence = 0;
    static uint32_t oneSequence = 0;
    static bool seqMet = false;

    if (c == 0x00)
    {
        ++nullSequence;
        seqMet = nullSequence > (double)(FLUSH_BYTES) / 1.25;
    }
    else
    {
        if (seqMet)
        {
            if (c == FLUSH_REQUEST_BYTE)
            {
                if (++oneSequence == 5)
                {
                    seqMet = false;
                    nullSequence = 0;
                    oneSequence = 0;
                    return FLUSH_NEW_TRANSMISSION; // retransmit packet
                }
            }

            seqMet = false;
            nullSequence = 0;
            oneSequence = 0;
            return FLUSH_CRITICAL_ERROR;
        }
    }

    return FLUSH_NOOP;
};

uint8_t SPIClient::performFlushGet()
{
    static uint32_t nullSequence = 0;
    static uint32_t oneSequence = 0;

    if (nullSequence < (double)(FLUSH_BYTES) / 1.25)
    {
        nullSequence++;
        return 0x00;
    }

    if (++oneSequence == 5)
    {
        nullSequence = 0;
        oneSequence = 0;
        SET_MODE_SWITCH(START_UP);
    }

    return FLUSH_REQUEST_BYTE; // send one, or signal two
};

uint8_t SPIClient::performStartUpGet(bool consumeStateFreshness)
{
    static SPIPacketHeaderUnion header;
    static uint8_t byteNum = 0;

    if (modeSwitch.use(consumeStateFreshness))
    {
        header.header = SPIPacketHeader(START_UP_STRUCT, NONE);
        byteNum = 0;
    }

    // writing phase
    return header.buffer[byteNum];
}

void SPIClient::performStartUpUpdate(char c, bool consume)
{
    static SPIPacketHeaderUnion header;
    static uint8_t byteNum = 0;

    if (modeSwitch.use(consume))
    { // check 'first time'
        header.header = SPIPacketHeader(START_UP_STRUCT, NONE);
        byteNum = 0;
    }

    header.buffer[byteNum++] = c;

    if (byteNum == sizeof(SPIPacketHeader))
    {
        if (header.header.structType == START_UP_STRUCT && header.header.length == 0)
        {
            SET_MODE_SWITCH(STANDARD); // go ahead
        }
        else
        {
            SET_MODE_SWITCH(FLUSHING);
        }
    }

    return;
}

uint8_t SPIClient::performStandardGet()
{
    return msgGoingOut.getCurrentByte();
}

void SPIClient::performStandardUpdate(char c, SPIPacketHeaderUnion &header, char *dataBuf)
{
    static uint32_t byteIndex = 0;

    if (modeSwitch.use(true)) // first switch
    {
        byteIndex = 0;
    }

    // read packet
    if (byteIndex < sizeof(SPIPacketHeader))
    {
        header.buffer[byteIndex++] = c;
    }
    else if (byteIndex < NUM_BYTES_PER_PACKET)
    {
        dataBuf[byteIndex++ - sizeof(SPIPacketHeader)] = c;
    }

    // on complete
    if (byteIndex == NUM_BYTES_PER_PACKET)
    {
        ReadTransmissionState readState;
        if (verifyHeaderChecksum(header.header) && verifyDataChecksum(dataBuf, header.header))
        {
            readState = msgComingIn.writePacket(header.header, dataBuf);
        }
        else
        {
            // no clue what's happening, just resend stuff
            readState.incomingRequest = NONE;
            readState.outgoingRequest = NONE;
        }

        byteIndex = 0;
        msgGoingOut.updateState(&readState);

        if (readState.incomingRequest == RETRY_TRANSMISSION || msgComingIn.readRetries + msgGoingOut.writeRetries >= PACKET_RETRY_LIMIT)
        {
            SET_MODE_SWITCH(FLUSHING);
        }

        return;
    }

    // brainlessly update
    msgGoingOut.updateState(nullptr);
}

uint8_t SPIClient::operate() {
    return 0x0;
}

uint8_t SPIMaster::operate()
{
    static SPIPacketHeaderUnion headerBuf;
    static char dataBuf[NUM_BYTES_PER_PACKET];

    // use mode to set variable states
    uint8_t packetRes;
    switch (mode)
    {
    case FLUSHING:
        packetRes = performFlushGet();
        if (modeSwitch.use())
        {
            msgGoingOut.reset();
            msgComingIn.reset();
        }
        break;
    case START_UP:
        packetRes = performStartUpGet(false);
        break;
    case STANDARD: // normal read and write
        packetRes = performStandardGet();
        break;
    }

    char spiReturn = SPI.transfer(packetRes);
    uint8_t slaveFlushState = handleFlush(spiReturn);

    // interpret whether we are flushing or not
    switch (slaveFlushState)
    {
    case FLUSH_NEW_TRANSMISSION:
    case FLUSH_CRITICAL_ERROR:
        if (!IS_MODE(FLUSHING))
        {
            SET_MODE_SWITCH(FLUSHING);
        }
        break;
    case FLUSH_NOOP: // all is well
        break;
    };

    switch (mode)
    {
    case FLUSHING:
        break;
    case START_UP:
        performStartUpUpdate(spiReturn, true); // very first byte of input is useless for both
        break;
    case STANDARD: // normal read and write
        performStandardUpdate(spiReturn, headerBuf, dataBuf);
        break;
    }

    return packetRes;
};

#if !defined(ESP8266)
uint8_t SPISlave::operate()
{
    static SPIPacketHeaderUnion headerBuf;
    static char dataBuf[NUM_BYTES_PER_PACKET];
    char spiReturn = SPDR; // read from the register ??

    uint8_t slaveFlushState = handleFlush(spiReturn);
    char packetRes;
    // interpret whether we are flushing or not
    switch (slaveFlushState)
    {
    case FLUSH_NEW_TRANSMISSION:
    case FLUSH_CRITICAL_ERROR:
        if (!IS_MODE(FLUSHING))
            SET_MODE_SWITCH(FLUSHING);
        break;
    case FLUSH_NOOP: // all is well
        break;
    };

    // update get and return
    switch (mode)
    {
    case FLUSHING:
        packetRes = performFlushGet();
        break;
    case START_UP:
        performStartUpUpdate(spiReturn, false);
        packetRes = performStartUpGet(true);
        break;
    case STANDARD: // normal read and write
        performStandardUpdate(spiReturn, headerBuf, dataBuf);
        packetRes = performStandardGet();
        break;
    }

    SPDR = packetRes;
    return packetRes;
}
#endif // (defined ESP8266)