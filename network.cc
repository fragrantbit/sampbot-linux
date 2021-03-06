

#include <cstdio>
#include <cstring>
#include "network.h"


#include "samp_encr.h"
#include "RakNet/GetTime.h"


#include "common.h"


#include "tables.h"

static void *tickEntry(void *self);
static void *blocksWrapperEntry(void *self); 



pthread_mutex_t blockImageMutex;
pthread_cond_t blockImageCond;

void Network::chargeKit()
{
    _sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    struct timeval read_timeout;

    remote->peer.sin_addr.s_addr    = inet_addr(remote->peerAddr);
    remote->peer.sin_port           = htons(remote->peerPort);
    remote->peer.sin_family         = AF_INET;

    local.sin_addr.s_addr           = inet_addr("0.0.0.0");
    local.sin_port                  = htons(0);
    local.sin_family                = AF_INET;

   
    if(bind(_sockfd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        _log("err");
    }   
    
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 1;

    setsockopt(
        _sockfd, 
        SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);

    
    /*pthread_create(&tickThread, 0, &tickEntry, this);
    pthread_create(&blocksWrapperThread, 0, &blocksWrapperEntry, this);*/
}

int Network::sendTo(char const *data, int length, bool handle)
{
    int len;
    kyretardizeDatagram((unsigned char *)data, length, remote->peerPort, 0);

    len = sendto(
        _sockfd, 
        (char *)encrBuffer, 
        length + 1, 
        0, 
        (struct sockaddr *)&remote->peer, 
        sizeof(struct sockaddr_in));


    return len;
}


// Testing function.
// Should ping the remote system once some time.
void Network::pingRemoteSystem()
{
    RakNet::BitStream outBitStream(sizeof(unsigned char)+sizeof(RakNetTime));
    RakNetTime sendPingTime = RakNet::GetTime();

    outBitStream.Write((unsigned char)ID_INTERNAL_PING);
    outBitStream.Write(sendPingTime);

    makePacket(outBitStream, UNRELIABLE, SYSTEM_PRIORITY, sendPingTime);
  
    sendTo(
        (char *)outBitStream.GetData(),
        outBitStream.GetNumberOfBytesUsed(), 
        false);
}

void Network::processBlock(struct DataBlock &block)
{  
    int len = block.len;

    unsigned char packetId = block.packetId;
    char const *data = (char const *)block.content;
    switch(packetId) {
        case ID_OPEN_CONNECTION_COOKIE: {
            unsigned short cookie = *(unsigned short *)(data);
            _datalog("Received connection cookie -> %d", cookie);

            sendCookie(data, cookie);
           
            return;
        }
        case ID_OPEN_CONNECTION_REPLY: {
            sendConnectionRequest();
            return;
        }
        case ID_CONNECTION_REQUEST_ACCEPTED: {
            RakNet::BitStream outBitStream(sizeof(unsigned char)+sizeof(unsigned int)+sizeof(unsigned short));
            
            if(!_connected) {

                unsigned int uiSvrChallenge;

                RakNet::BitStream inBitStream((unsigned char *)data, len, false);


                // c. No need to ignore bits. 
                // The first byte (packetId) is written separately.

                //inBitStream.IgnoreBits(8); 

                inBitStream.Read(externalID.binaryAddress);
                inBitStream.Read(externalID.port);
                inBitStream.Read(playerIndex);
                inBitStream.Read(uiSvrChallenge);

                setUISvrChallenge(uiSvrChallenge);   
                
                outBitStream.Write((unsigned char)ID_NEW_INCOMING_CONNECTION);
                outBitStream.Write(externalID.binaryAddress);
                outBitStream.Write(externalID.port);
                
                RakNetTime timeMS = RakNet::GetTime();
                makePacket(outBitStream, RELIABLE, SYSTEM_PRIORITY, timeMS);

                sendTo(
                    (char *)outBitStream.GetData(),
                    outBitStream.GetNumberOfBitsUsed(),
                    false);


                if(!_connected) 
                    bundle->clientManager()->onConnectionAccepted(
                        (char const *)inBitStream.GetData(), 
                        len, 
                        externalID.binaryAddress,
                        externalID.port);

                _connected = true;
            }
         //   } 

            /*outBitStream.Write((unsigned char)ID_NEW_INCOMING_CONNECTION);
            outBitStream.Write(externalID.binaryAddress);
            outBitStream.Write(externalID.port);
            timeMS = RakNet::GetTime();
            makePacket(outBitStream, RELIABLE, SYSTEM_PRIORITY, timeMS);

            sendTo(
            (char *)outBitStream.GetData(),
            outBitStream.GetNumberOfBitsUsed(),
            false);*/
            //pingRemoteSystem();
            return;
        }
        case ID_AUTH_KEY: {
            if(!isConnected()) {
                bundle->clientManager()->sendAuthKey((char const *)data, len);
            }
            return; 
        }
        case ID_RPC: {
            
            // _datalog("rpc");
            if(isConnected()) 
                bundle->rpcManager()->handleRPC((char const *)data, len);
            
        }
        case ID_CONNECTION_BANNED: {
           // _datalog("BANNED");
            //_datalog("%d %d %d\n", packetId, data[0], data[1]);
            return;
        }
        case ID_RPC_REPLY: {
            _datalog("ID_RPC_REPLY");
            return;
        }

        /*case ID_INTERNAL_PING: {

            _datalog("ID_INTERNAL_PING");
            RakNet::BitStream outBitStream;
            RakNet::BitStream inBitStream((unsigned char *)data, len, false);
            RakNetTime sendPingTime;

            // inBitStream.IgnoreBits(8);
            inBitStream.Read(sendPingTime);   
            outBitStream.Write((unsigned char)ID_CONNECTED_PONG);
            outBitStream.Write(sendPingTime);
            RakNetTime timeMS = RakNet::GetTime();

            outBitStream.Write(timeMS);

            makePacket(outBitStream, UNRELIABLE, SYSTEM_PRIORITY, timeMS);
            /*sendTo(
                (char *)outBitStream.GetData(),
                outBitStream.GetNumberOfBytesUsed(),
                false);*/
            
           // pingRemoteSystem();
           /* return;

        }*/
        case ID_RECEIVED_STATIC_DATA: {
            // Need to inform game server client received static data.
            RakNet::BitStream staticData;
            staticData.Write((unsigned char)ID_RECEIVED_STATIC_DATA);
            staticData.WriteBits((const unsigned char* )data, len);
            RakNetTime timeMS = RakNet::GetTime();
            makePacket(staticData, RELIABLE, HIGH_PRIORITY, timeMS);

            sendTo(
                (char *)staticData.GetData(), 
                staticData.GetNumberOfBitsUsed(),
                false);

            return;
        }
        case ID_NEW_INCOMING_CONNECTION: {
            _datalog("ID_NEW_INCOMING_CONNECTION");
            return;
        }
        case ID_DISCONNECTION_NOTIFICATION: {
            _log("Lose connection");
            return;
        }
        // Do more research on the ping.
        case ID_PING_OPEN_CONNECTIONS: {
            _datalog("ID_PING_OPEN_CONNECTIONS");
            RakNet::BitStream inBitStream((unsigned char *)data, len, false);
            //inBitStream.IgnoreBits(8);
            RakNetTime sendPingTime;
            inBitStream.Read(sendPingTime);
            RakNet::BitStream outBitStream;
            outBitStream.Write((unsigned char)ID_PONG); 
            outBitStream.Write(sendPingTime);
            outBitStream.Write(sendPingTime+sendPingTime*sendPingTime);
            //sendTo((char *)outBitStream.GetData(), outBitStream.GetNumberOfBytesUsed(), false);
            return;
        }

        case ID_PING: {
            RakNet::BitStream inBitStream((unsigned char *)data, len, false);
            // inBitStream.IgnoreBits(8);  
            RakNetTime sendPingTime;
            inBitStream.Read(sendPingTime);
            RakNet::BitStream outBitStream;
            outBitStream.Write((unsigned char)ID_PONG);
            outBitStream.Write(sendPingTime);
            outBitStream.Write(sendPingTime+sendPingTime*sendPingTime);
           // sendTo((char *)outBitStream.GetData(), outBitStream.GetNumberOfBytesUsed(), false);
            pingRemoteSystem();
            printf("ID_PING\n");
            return;
        }
        case ID_PONG: {
            _datalog("ID_PONG");
            return;
        }
        case ID_CONNECTED_PONG: {
            //_datalog("ID_CONNECTED_PONG");
            //pingRemoteSystem();
            return;
        }
        case 227: {
            return;
        }
        /*default: {
            _log("unhandled packet id - %d", data[0]);
            return;
        }*/

    }
}

InternalPacket *Network::makeIPacket(const RakNet::BitStream &bs, 
        PacketReliability reliability, 
        PacketPriority priority,
        long long time)
{
    int numberOfBytesToSend = BITS_TO_BYTES(bs.GetNumberOfBitsUsed());

    InternalPacket *internalPacket      = internalPacketPool.GetPointer();
   
    internalPacket->data                = new unsigned char[numberOfBytesToSend];   
    
    memcpy(internalPacket->data, bs.GetData(), numberOfBytesToSend);

    internalPacket->dataBitLength       = bs.GetNumberOfBitsUsed();
    internalPacket->nextActionTime      = 0;
    internalPacket->messageNumber       = statistics.messagesSent[0];
    internalPacket->priority            = priority;
    internalPacket->reliability         = reliability;
    internalPacket->splitPacketCount    = 0;
    internalPacket->creationTime        = time;

    return internalPacket;
    
}

int Network::recvFrom(char *data)
{
    int len;

    len = recvfrom(_sockfd, data, MAXIMUM_MTU_SIZE, 0, 0, 0);
    return len;
}


DataBlock *Network::createBlock(char const *data, int len) 
{
    struct DataBlock *block = new DataBlock;

    RakNet::BitStream newBS((unsigned char *)data, len, false);
    
    
    RakNetTimeNS time = RakNet::GetTimeNS();
    bool hasAcks = false;

    newBS.Read(hasAcks);

    DataStructures::RangeList<MessageNumberType> incomingAcks;
    if(hasAcks) 
        if(!incomingAcks.Deserialize(&newBS))
            return 0;

    InternalPacket *internalPacket = getIPFromBS(&newBS, time);

    unsigned char packetId = 0;
    
    (
        internalPacket ? packetId = internalPacket->data[0] : packetId = data[0]
    );

    block->write(packetId);
    const unsigned char *dataSource;

    (
        internalPacket ? dataSource = internalPacket->data : dataSource = (unsigned char *)data
    );

    block->content = new unsigned char[len];

    for(int i = 1; i < len; i++) {
        block->write(dataSource[i]);
    }

    block->len = block->bytesCopied;

    return block;
}

void *Network::networkUpdateLoop()
{
    char *data = new char[4096];
    int *len = new int;
    for(;;) { 
        *len = recvFrom(data); 
        if(*len != -1) {
            DataBlock *image = createBlock(data, *len);
            if(image) {
                _blockImg = image;
                pthread_cond_signal(&blockImageCond);
            }
        }
    }
    return 0;
}

void Network::onInternalPingLoop() {
    pingRemoteSystem();
}

void Network::requestCookie()
{
    unsigned char c[3];

    _log("Connecting to %s:%d...", remote->peerAddr, remote->peerPort);
    _datalog("Requesting cookie");

    c[0] = 24;
    sendTo((char const *)c, sizeof(c));
}

void Network::sendCookie(char const *&data, unsigned short cookie) 
{
    unsigned char c[3]; 
    c[0] = 24;
    *(unsigned short *)&c[1] = cookie ^ 0x6969;

    _datalog("Sending cookie. [%d:%d]", c[0], cookie);

    sendTo((char const *)c, sizeof(c));
}

void Network::initRequest()
{
    requestCookie();
}

void Network::sendConnectionRequest() 
{
    RakNet::BitStream bs;
                    
    bs.Write((unsigned char)ID_CONNECTION_REQUEST);
    makePacket(bs);
   
    _datalog("Sending connection request [bsLen: %d]", bs.GetNumberOfBitsUsed());
    _datalog("bytelen: %d", bs.GetNumberOfBytesUsed());

    sendTo((char *)bs.GetData(), bs.GetNumberOfBytesUsed(), false);
}

void *Network::blocksWrapper() 
{
    for(;;) {
        pthread_mutex_lock(&blockImageMutex);
        while(_blockImg == 0) 
            pthread_cond_wait(&blockImageCond, &blockImageMutex);

        /* 
            Process this block once constructed.
            pthread_cond_signal(blockImageCond) in networkUpdateLoop is the only path.
        */
        
        processBlock(*_blockImg); 
        _blockImg = 0;
        pthread_mutex_unlock(&blockImageMutex);
    }
}
void Network::makePacket(RakNet::BitStream &bs)
{
    RakNetTime time = RakNet::GetTime();

    InternalPacket *IPacket = makeIPacket(
        bs, 
        RELIABLE, 
        SYSTEM_PRIORITY,
        time);

    GenerateDatagram(
        &bs, 
        *IPacket, 
        MAXIMUM_MTU_SIZE, 
        time);
}

void Network::makePacket(
        RakNet::BitStream &bs, PacketReliability reliability, 
        PacketPriority priority, RakNetTime timeMS)
{

    DataStructures::List<PluginInterface *> messageHandlerList;
    //RakNetTime time = RakNet::GetTime();

    InternalPacket *IPacket = makeIPacket(
        bs, 
        reliability, 
        priority, 
        timeMS);

    GenerateDatagram(
        &bs, 
        *IPacket, 
        MAXIMUM_MTU_SIZE,            
        timeMS);
}

void DataBlock::write(char const byte)
{
    if(this->bytesCopied == 0) {
        this->packetId = byte;
    } else {
        this->content[this->bytesCopied - 1] = byte;
    }

    this->bytesCopied++;

}

void Network::connect() 
{    
    initRequest();
}
