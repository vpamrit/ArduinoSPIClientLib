#include <MessageQueue.h>

/** MessageToBeSent **/
volatile char MessageToBeSent::out[500] = {};
volatile uint32_t MessageToBeSent::size = 0;
volatile uint16_t MessageToBeSent::id = 0;

// variables to expose internal state
volatile bool MessageToBeSent::full = false;
volatile bool MessageToBeSent::ready = false;
volatile bool MessageToBeSent::isMaster = SPIConfig::isMaster; //TODO: init this somehow???
volatile uint8_t MessageToBeSent::packetNum = 0;

//outgoing piggyback
volatile ConsumableByte MessageToBeSent::piggyback;
volatile uint16_t MessageToBeSent::nextPacketPos = 0;           // this is theirs
volatile uint16_t MessageToBeSent::ourExpectedNextPacket = 0;   // this is ours
volatile uint16_t MessageToBeSent::theirExpectedNextPacket = 0; // this is ours

volatile uint32_t MessageToBeSent::dataByteNum = 0;
volatile uint8_t MessageToBeSent::headerByteNum = 0;
volatile MessageToBeSent::State MessageToBeSent::msgMode= State::HEADER;

volatile uint16_t MessageToBeSent::writeRetries = 0;
SPIPacketHeader MessageToBeSent::nextHeader;
SPIPacketHeaderUnion MessageToBeSent::currentHeader;

bool MessageToBeSent::acceptTransmission(uint8_t structType, char *dataBuffer, uint32_t length, bool force)
{
    if (!force && full)
    {
        return false;
    }

    refresh();
    memcpy(dataBuffer, (char*)out, length);    // this could turn current transmit into gibberish, but it's forced so...
                                        // we can handle this by dropping the expectedPacketPos != header.id
    nextHeader.structType = structType; // save for blank packet case
    nextHeader.numPackets = ceil((double)length / NUM_BYTES_DATA);
    size = length;

    //TODO: I think this will be overwritten
    if (force)
    {
        nextHeader.request = FORCE_NEW_TRANSMIT;
    }

    // mark as full
    full = true;
    return true;
}

uint8_t MessageToBeSent::getCurrentByte()
{
    switch (msgMode)
    {
    case State::HEADER:
        return currentHeader.buffer[headerByteNum];
    case State::BLANK_BODY:
        return 0x01;
    case State::BODY:
        return out[dataByteNum + packetNum * NUM_BYTES_DATA];
    default:
        Serial.println("IMPOSSIBLE MODE REACHED");
        exit(1);
    }
}

void MessageToBeSent::updateState(ReadTransmissionState *state)
{
    switch (msgMode)
    {
    case State::HEADER:
        ++headerByteNum;
        if (headerByteNum == NUM_BYTES_HEADER)
        {
            msgMode = currentHeader.header.request == BLANK_PACKET ? State::BLANK_BODY : State::BODY;
        }
        break;
    case State::BLANK_BODY:
        ++dataByteNum;
        if (dataByteNum == NUM_BYTES_DATA)
        {
            msgMode = State::HEADER;
            headerByteNum = 0;
        }
        break;
    case State::BODY:
        ++dataByteNum;
        // transition state
        if (dataByteNum == NUM_BYTES_DATA)
        {
            msgMode = State::HEADER;
            headerByteNum = 0; // CRITICAL
        }

        break;
    }

    // init next header after body is ready for next header
    if (headerByteNum == 0)
    {
        initNextHeader(state);
    }
}

void MessageToBeSent::initNextHeader(ReadTransmissionState *state)
{
    headerByteNum = 0;
    dataByteNum = 0;
    bool sendBlank = false;

    // since we forcibly injected the new one
    // we will switch
    currentHeader.header = nextHeader;

    SPIPacketHeader &header = currentHeader.header;
    if (state == nullptr)
    {
        Serial.println("Did not get refreshed transmission from state -- quitting");
        exit(1);
    }

    uint16_t thePacketWeThinkShouldHaveBeenNextForThem = packetNum;
    bool resendPacket = thePacketWeThinkShouldHaveBeenNextForThem != state->theyExpectToReceiveFromUs;

    // process incoming piggybacks
    switch (state->incomingRequest)
    {
    case NONE: //TODO: is there any difference in how want to handle this
    case PACKET_COMPLETE:
        break;
    case TRANSMIT_COMPLETE:
        if (packetNum >= header.numPackets)
        {              // means we've been sending blank packets
            refresh(); // mark this as complete and we're ready for a new one
            packetNum = 0;
        }
        else
        {
            // we might have forced at the very end of the previous packet so...
        }
        break;
    case UNAVAILABLE:
        sendBlank = true;
        break;
    }

    //TODO: need to switch mode to flushing with writeRetries
    // let's figure out what we need to resend them because we're not getting an ACK
    if (resendPacket)
    {
        ++writeRetries;
        Serial.println("Retrying given packet");
        packetNum = state->theyExpectToReceiveFromUs;
    }
    else
    {
        // increment packet num because we assume all is well
        ++packetNum;
    }

    uint16_t dataBytePos = packetNum * NUM_BYTES_DATA;

    // not needed because we save this in nextHeader
    // header.id = id;
    // header.structType = structType;
    header.request = state->outgoingRequest;
    header.expectedNextPacketPos = state->weExpectToReceiveFromThem;
    header.packetPos = packetNum;
    header.length = min(size - dataBytePos, (uint)NUM_BYTES_DATA);
    header.headerChecksum = computeHeaderChecksum(header);

    // first packet, forced transmit case
    if (nextHeader.request == FORCE_NEW_TRANSMIT && packetNum == 0)
    {
        header.request = header.request | FORCE_NEW_TRANSMIT;
    }

    // how to handle case where we haven't received transmit complete
    // how to handle case where we have nothing to send
    sendBlank = sendBlank || packetNum >= header.numPackets || !full;
    if (sendBlank)
    {
        ++writeRetries;
        header.structType = BLANK_PACKET;
        header.length = 0;
        header.checksum = 0x01; // don't bother
        return;
    }

    header.checksum = computeDataChecksum(out + dataBytePos, header.length);
}

void MessageToBeSent::reset()
{
    msgMode = State::HEADER;
    dataByteNum = 0;
    headerByteNum = 0;
    packetNum = 0;
    writeRetries = 0;

    //TODO: FIGURE OUT HOW THIS WILL WORK!!! WE HAVE NO PREVIOUS DATA!
    ReadTransmissionState state;
    state.incomingRequest = NONE;
    state.outgoingRequest = NONE;
    state.weExpectToReceiveFromThem = 0;
    state.theyExpectToReceiveFromUs = 0;
    initNextHeader(&state);
}

void MessageToBeSent::refresh()
{
    SPIPacketHeader& header = currentHeader.header;
    full = false;
    uint16_t newId;
    do
    {
        newId = random(255);
    } while (newId != header.id);

    header.structType = BLANK_PACKET;
    header.id = newId;
    reset();
}

/** MessageToBeReceived **/
volatile char MessageToBeReceived::in[500] = {};
volatile bool MessageToBeReceived::full = false;
volatile bool MessageToBeReceived::ready = false;
volatile bool MessageToBeReceived::isMaster = SPIConfig::isMaster; //TODO: not sure if this is guaranteed order for static init
volatile uint8_t MessageToBeReceived::structType;
volatile uint32_t MessageToBeReceived::size = 0;
volatile uint32_t MessageToBeReceived::weExpectToReceiveFromThem = 0;
volatile int32_t MessageToBeReceived::theyExpectToReceiveFromUs = 0;
volatile uint32_t MessageToBeReceived::lastReceivedPacket = 0;
volatile uint16_t MessageToBeReceived::id = 0;
volatile int16_t MessageToBeReceived::finalDataChecksum = -1;
volatile uint32_t MessageToBeReceived::readRetries = 0;

Metadata MessageToBeReceived::readMessage(char *buf)
{
    Metadata mdata;
    if (!ready)
    {
        mdata.checksum = -1;
        mdata.size = 0;
        return mdata;
    }

    memcpy(buf, (char*)in, size);
    mdata.checksum = finalDataChecksum;
    mdata.size = size;
    return mdata;
}

ReadTransmissionState MessageToBeReceived::writePacket(SPIPacketHeader &header, char *dataBuffer)
{
    ReadTransmissionState state;
    state.incomingRequest = NONE;
    state.outgoingRequest = NONE;
    state.weExpectToReceiveFromThem = weExpectToReceiveFromThem;
    state.theyExpectToReceiveFromUs = theyExpectToReceiveFromUs;

    // TODO: is this fine? packet used when other person has nothing to say or do
    bool isStartUpStruct = header.structType == START_UP_STRUCT;
    bool isBlankPacket = header.structType == BLANK_PACKET;
    bool forcedTransmit = header.request & FORCE_NEW_TRANSMIT;

    // turn off the force transmit
    header.request = header.request & ~(FORCE_NEW_TRANSMIT);

    bool newTransmit = !full || forcedTransmit;

    // start up structs and blank packets are special cases
    // refresh our state and set expected packet to zero when we receive a startup struct
    // otherwise just read the incoming request and expected packet pos
    if (isStartUpStruct || newTransmit)
    {
        refresh();
    }

    if (isStartUpStruct || isBlankPacket)
    {
        state.incomingRequest = header.request;
        state.theyExpectToReceiveFromUs = header.expectedNextPacketPos;
        return state;
    }

    // see if this ia forced transmit

    if (newTransmit || (header.id == id && header.structType == structType)) // request force new transmit
    {
        id = header.id; // set transaction id
        structType = header.structType;
        full = true;
    }
    else
    {
        Serial.println("UNABLE TO START NEW TRANSACTION");
        state.outgoingRequest = UNAVAILABLE;
        return state;
    }

    // critical packet error checks
    if (newTransmit && header.packetPos != 0)
    {
        Serial.println("New transmission but packetPos was not zero!");
        state.incomingRequest = RETRY_TRANSMISSION;
        return state;
    }
    if (header.length != NUM_BYTES_DATA && header.packetPos != header.numPackets - 1)
    {
        Serial.println("Malformed or corrupted packet");
        state.incomingRequest = RETRY_TRANSMISSION;
        return state;
    }

    // you sent us the right packet
    if (weExpectToReceiveFromThem == header.packetPos)
    {
        ++weExpectToReceiveFromThem;
    }
    else
    {
        ++readRetries;
    }

    // compute where we should place this packet
    uint32_t index = header.packetPos * NUM_BYTES_DATA;
    size = index + header.length;

    memcpy((char*)in + index, dataBuffer, header.length); //copy data into message buffer

    // update last known values
    theyExpectToReceiveFromUs = header.expectedNextPacketPos;
    ready = header.packetPos + 1 == header.numPackets;

    if (ready)
    {
        state.outgoingRequest = TRANSMIT_COMPLETE;
        finalDataChecksum = computeDataChecksum((char*)in, size);
        return state;
    }

    // reassign values after update
    state.outgoingRequest = PACKET_COMPLETE;
    return state;
}

bool MessageToBeReceived::consume()
{
    if (ready)
    {
        refresh();
        return true;
    }

    return false;
}

void MessageToBeReceived::reset()
{
    weExpectToReceiveFromThem = 0;
    theyExpectToReceiveFromUs = 0;
    readRetries = 0;
    ready = false;
    finalDataChecksum = -1;
}

void MessageToBeReceived::refresh()
{
    size = 0;
    full = false;
    reset();
}