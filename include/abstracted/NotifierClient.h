#ifndef NOTIFIER_CLIENT_H
#define NOTIFIER_CLIENT_H

#include <Arduino.h>
#include "SPIClient.h"

struct Client_Packet
{

//     Client_Packet(uint8_t curr_time, uint8_t opName, uint8_t fingerId, uint8_t confidence)
//     {
//         this->curr_time = curr_time;
//         this->operationName = opName;
//         this->fingerId = fingerId;
//     }

//     uint8_t curr_time;
//     uint8_t operationName;
//     uint8_t fingerId;
//     uint8_t confidence;
};

class NotifierClient
{
public:
    bool sendFingerConfirmation(uint8_t fingerId, uint8_t confidence);
    bool init(void);
    bool lastRequestSucceeded;
    bool sendMessage();
    bool readMessage();
    void update();
    uint8_t lastKnownTime;

private:
    static SPISlave client;
    bool writeStructuredPacket(const Client_Packet &p);
    bool readStructuredPacket(const Client_Packet &p);
};

#endif //NOTIFIER_CLIENT_H