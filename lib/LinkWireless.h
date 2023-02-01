#ifndef LINK_WIRELESS_H
#define LINK_WIRELESS_H

// --------------------------------------------------------------------------
// // TODO: COMPLETE An SPI handler for the Link Port (Normal Mode, 32bits).
// --------------------------------------------------------------------------
// // TODO: Usage:
// - 1) Include this header in your main.cpp file and add:
//       LinkWireless* linkWireless = new LinkWireless();
// - 2) Initialize the library with:
//       linkWireless->activate();
// - 3) Exchange 32-bit data with the other end:
//       _________
//       // (this blocks the console indefinitely)
// - 4) Exchange data with a cancellation callback:
//       u32 data = linkGPIO->transfer(0x1234, []() {
//         u16 keys = ~REG_KEYS & KEY_ANY;
//         return keys & KEY_START;
//       });
// --------------------------------------------------------------------------
// considerations:
// - when using Normal Mode between two GBAs, use a GBC Link Cable!
// - only use the 2Mbps mode with custom hardware (very short wires)!
// --------------------------------------------------------------------------

#include <tonc_core.h>
#include <functional>  // TODO: REMOVE
#include <string>      // TODO: REMOVE
#include <vector>
#include "LinkGPIO.h"
#include "LinkSPI.h"

#define LINK_WIRELESS_PING_WAIT 50
#define LINK_WIRELESS_TRANSFER_WAIT 15
#define LINK_WIRELESS_BROADCAST_SEARCH_WAIT ((160 + 68) * 60)
#define LINK_WIRELESS_TIMEOUT 100
#define LINK_WIRELESS_LOGIN_STEPS 9
#define LINK_WIRELESS_BROADCAST_SIZE 6
#define LINK_WIRELESS_COMMAND_HEADER 0x9966
#define LINK_WIRELESS_RESPONSE_ACK 0x80
#define LINK_WIRELESS_DATA_REQUEST 0x80000000
#define LINK_WIRELESS_SETUP_MAGIC 0x003c0420
#define LINK_WIRELESS_COMMAND_HELLO 0x10
#define LINK_WIRELESS_COMMAND_SETUP 0x17
#define LINK_WIRELESS_COMMAND_BROADCAST 0x16
#define LINK_WIRELESS_COMMAND_START_HOST 0x19
#define LINK_WIRELESS_COMMAND_IS_CONNECT_ATTEMPT 0x1a
#define LINK_WIRELESS_COMMAND_BROADCAST_READ_START 0x1c
#define LINK_WIRELESS_COMMAND_BROADCAST_READ_END 0x1e
#define LINK_WIRELESS_COMMAND_CONNECT 0x1f
#define LINK_WIRELESS_COMMAND_IS_FINISHED_CONNECT 0x20
#define LINK_WIRELESS_COMMAND_FINISH_CONNECTION 0x21
#define LINK_WIRELESS_COMMAND_SEND_DATA 0x24
#define LINK_WIRELESS_COMMAND_RECEIVE_DATA 0x26

const u16 LINK_WIRELESS_LOGIN_PARTS[] = {0x494e, 0x494e, 0x544e, 0x544e, 0x4e45,
                                         0x4e45, 0x4f44, 0x4f44, 0x8001};

class LinkWireless {
 public:
  struct ClientIdResponse {
    bool success = false;
    u16 clientId = 0;
  };
  struct ClientIdResponses {
    bool success = false;
    std::vector<u32> clientIds = std::vector<u32>{0};  // TODO: IMPROVE
  };
  std::function<void(std::string)> debug;  // TODO: REMOVE

  bool isActive() { return isEnabled; }

  // TODO: Add to docs (bool)
  bool activate() {
    if (!reset()) {
      deactivate();
      return false;
    }

    isEnabled = true;
    return true;
  }

  void deactivate() {
    isEnabled = false;
    stop();
  }

  bool host(std::vector<u32> data) {
    if (data.size() != LINK_WIRELESS_BROADCAST_SIZE)
      return false;

    if (!sendCommand(LINK_WIRELESS_COMMAND_SETUP, std::vector<u32>{0x003C0420})
             .success)
      return false;

    return sendCommand(LINK_WIRELESS_COMMAND_BROADCAST, data).success &&
           sendCommand(0x13).success &&
           sendCommand(LINK_WIRELESS_COMMAND_START_HOST).success;
  }

  ClientIdResponses acceptConnection() {
    auto result = sendCommand(LINK_WIRELESS_COMMAND_IS_CONNECT_ATTEMPT);

    ClientIdResponses response;
    if (!result.success)
      return response;
    response.success = true;
    if (result.responses.size() == 0)
      return response;
    response.clientIds = result.responses;

    sendCommand(0x13);  // meh

    return response;
  }

  bool connect(u16 remoteId) {
    return sendCommand(LINK_WIRELESS_COMMAND_CONNECT,
                       std::vector<u32>{remoteId})
        .success;
  }

  ClientIdResponse checkConnection() {
    auto result = sendCommand(LINK_WIRELESS_COMMAND_IS_FINISHED_CONNECT);

    ClientIdResponse response;
    if (!result.success)
      return response;
    response.success = true;
    if (result.responses.size() == 0 || msB32(result.responses[0]) > 0)
      return response;
    response.clientId = (u16)result.responses[0];

    return response;
  }

  ClientIdResponse finishConnection() {
    auto result = sendCommand(LINK_WIRELESS_COMMAND_FINISH_CONNECTION);

    ClientIdResponse response;
    if (!result.success)
      return response;
    response.success = true;
    if (result.responses.size() == 0)
      return response;
    response.clientId = (u16)result.responses[0];

    return response;
  }

  // TODO: CHECK RANGES
  bool sendData(std::vector<u32> data) {
    return sendCommand(LINK_WIRELESS_COMMAND_SEND_DATA, data).success;
  }

  bool sendDataWait(std::vector<u32> data) {
    // TODO: IT WORKS RANDOMLY
    // TODO: TEST THIS ON BOTH SIDES

    if (!sendCommand(0x25, data).success)  // TODO: SEND DATA WAIT
      return false;

    linkSPI->setSlaveMode2();

    u32 command = linkSPI->transfer(LINK_WIRELESS_DATA_REQUEST);
    while (!linkSPI->_isSIHigh())
      ;

    if (msB32(command) != LINK_WIRELESS_COMMAND_HEADER) {
      debug("NO HEADER: " + std::to_string(command));
      while (true)
        ;
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      return false;
    }
    if (lsB32(command) == 0x0027) {  // or 27 = inversion end
      command = linkSPI->transfer(0x996600a7);
      debug(">> RECEIVED: " + std::to_string(command));
      while (!linkSPI->_isSIHigh())
        ;
      if (command != LINK_WIRELESS_DATA_REQUEST) {
        debug("NO DATA REQUEST (END): " + std::to_string(command));
        while (true)
          ;
        linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
        return false;
      }

      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);

      wait(LINK_WIRELESS_TRANSFER_WAIT);
      return true;
    }

    if (lsB32(command) == 0x0128) {
      linkSPI->transfer(LINK_WIRELESS_DATA_REQUEST);
      while (!linkSPI->_isSIHigh())
        ;
    }

    if (lsB32(command) != 0x0128 &&
        lsB32(command) != 0x0028) {                // TODO: WAIT END RESPONSE
      debug("NO 28: " + std::to_string(command));  // TODO: OR 29 = disconnected
      while (true)
        ;
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      return false;
    }

    // TODO: WAIT END ACK
    command = linkSPI->transfer(0x996600a8);
    while (!linkSPI->_isSIHigh())
      ;
    linkSPI->_setSOLow();

    // // TODO: WAIT END ACK
    if (command != LINK_WIRELESS_DATA_REQUEST) {
      debug("NO WAIT: " + std::to_string(command));
      while (true)
        ;
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      return false;
    }

    linkSPI->setMasterMode2();
    linkSPI->set2MbpsSpeed2();

    return true;
  }

  bool receiveData(std::vector<u32>& data) {
    if (!sendCommand(0x11).success)  // TODO: SIGNAL LEVEL
      return false;

    auto result = sendCommand(LINK_WIRELESS_COMMAND_RECEIVE_DATA);
    data = result.responses;
    return result.success;
  }

  bool getBroadcasts(std::vector<u32>& data) {
    if (!sendCommand(LINK_WIRELESS_COMMAND_SETUP, std::vector<u32>{0x003f0420})
             .success)
      return false;

    if (!sendCommand(LINK_WIRELESS_COMMAND_BROADCAST,
                     std::vector<u32>{0x43490202, 0x4c432045, 0x45424d49,
                                      0x8a000052, 0x544e494e, 0x4f444e45})
             .success)
      return false;  // TODO: NOT NEEDED?

    if (!sendCommand(LINK_WIRELESS_COMMAND_BROADCAST_READ_START).success)
      return false;

    wait(LINK_WIRELESS_BROADCAST_SEARCH_WAIT);

    auto result = sendCommand(LINK_WIRELESS_COMMAND_BROADCAST_READ_END);
    data = result.responses;
    return result.success;
  }

  ~LinkWireless() {
    delete linkSPI;
    delete linkGPIO;
  }

 private:
  struct LoginMemory {
    u16 previousGBAData = 0xffff;
    u16 previousAdapterData = 0xffff;
  };

  struct CommandResult {
    bool success = false;
    std::vector<u32> responses = std::vector<u32>{};
  };

  LinkSPI* linkSPI = new LinkSPI();
  LinkGPIO* linkGPIO = new LinkGPIO();
  bool isEnabled = false;

  bool reset() {
    stop();
    return start();
  }

  bool start() {
    pingAdapter();
    linkSPI->activate(LinkSPI::Mode::MASTER_256KBPS);

    if (!login())
      return false;

    wait(LINK_WIRELESS_TRANSFER_WAIT);

    if (!sendCommand(LINK_WIRELESS_COMMAND_HELLO).success)
      return false;

    linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
    return true;
  }

  void stop() { linkSPI->deactivate(); }

  void pingAdapter() {
    linkGPIO->setMode(LinkGPIO::Pin::SO, LinkGPIO::Direction::OUTPUT);
    linkGPIO->setMode(LinkGPIO::Pin::SD, LinkGPIO::Direction::OUTPUT);
    linkGPIO->writePin(LinkGPIO::SD, true);
    wait(LINK_WIRELESS_PING_WAIT);
    linkGPIO->writePin(LinkGPIO::SD, false);
  }

  bool login() {
    LoginMemory memory;

    if (!exchangeLoginPacket(LINK_WIRELESS_LOGIN_PARTS[0], 0, memory))
      return false;

    for (u32 i = 0; i < LINK_WIRELESS_LOGIN_STEPS; i++) {
      if (!exchangeLoginPacket(LINK_WIRELESS_LOGIN_PARTS[i],
                               LINK_WIRELESS_LOGIN_PARTS[i], memory))
        return false;
    }

    return true;
  }

  bool exchangeLoginPacket(u16 data,
                           u16 expectedResponse,
                           LoginMemory& memory) {
    u32 packet = buildU32(~memory.previousAdapterData, data);
    u32 response = transfer(packet, false);

    if (msB32(response) != expectedResponse ||
        lsB32(response) != (u16)~memory.previousGBAData)
      return false;

    memory.previousGBAData = data;
    memory.previousAdapterData = expectedResponse;

    return true;
  }

  CommandResult sendCommand(u8 type,
                            std::vector<u32> params = std::vector<u32>{}) {
    CommandResult result;
    u16 length = params.size();
    u32 command = buildCommand(type, length);

    if (transfer(command) != LINK_WIRELESS_DATA_REQUEST)
      return result;

    for (auto& param : params) {
      if (transfer(param) != LINK_WIRELESS_DATA_REQUEST)
        return result;
    }

    u32 response = transfer(LINK_WIRELESS_DATA_REQUEST);
    u16 header = msB32(response);
    u16 data = lsB32(response);
    u8 responses = msB16(data);
    u8 ack = lsB16(data);
    if (header != LINK_WIRELESS_COMMAND_HEADER) {
      // TODO: REMOVE
      debug("HEADER FAILED! " + std::to_string(response));
      while (true)
        ;
      return result;
    }
    if (ack != type + LINK_WIRELESS_RESPONSE_ACK) {
      // TODO: REMOVE
      debug("ACK FAILED! " + std::to_string(response));
      while (true)
        ;
      return result;
    }

    for (u32 i = 0; i < responses; i++)
      result.responses.push_back(transfer(LINK_WIRELESS_DATA_REQUEST));

    result.success = true;
    return result;
  }

  u32 buildCommand(u8 type, u8 length = 0) {
    return buildU32(LINK_WIRELESS_COMMAND_HEADER, buildU16(length, type));
  }

  u32 transfer(u32 data, bool customAck = true) {
    if (!customAck)
      wait(LINK_WIRELESS_TRANSFER_WAIT);

    u32 lines = 0;
    u32 vCount = REG_VCOUNT;
    u32 receivedData = linkSPI->transfer(
        data, [this, &lines, &vCount]() { return timeout(lines, vCount); },
        false, customAck);

    lines = 0;
    vCount = REG_VCOUNT;
    if (customAck) {
      linkSPI->_setSOLow();
      while (!linkSPI->_isSIHigh())
        if (timeout(lines, vCount))
          return LINK_SPI_NO_DATA;
      linkSPI->_setSOHigh();
      while (linkSPI->_isSIHigh())
        if (timeout(lines, vCount))
          return LINK_SPI_NO_DATA;
      linkSPI->_setSOLow();
    }

    return receivedData;
  }

  bool timeout(u32& lines, u32& vCount) {
    if (REG_VCOUNT != vCount) {
      lines++;
      vCount = REG_VCOUNT;
    }

    return lines > LINK_WIRELESS_TIMEOUT;
  }

  void wait(u32 verticalLines) {
    u32 lines = 0;
    u32 vCount = REG_VCOUNT;

    while (lines < verticalLines) {
      if (REG_VCOUNT != vCount) {
        lines++;
        vCount = REG_VCOUNT;
      }
    };
  }

  u32 buildU32(u16 msB, u16 lsB) { return (msB << 16) | lsB; }
  u16 buildU16(u8 msB, u8 lsB) { return (msB << 8) | lsB; }
  u16 msB32(u32 value) { return value >> 16; }
  u16 lsB32(u32 value) { return value & 0xffff; }
  u8 msB16(u16 value) { return value >> 8; }
  u8 lsB16(u16 value) { return value & 0xff; }
};

extern LinkWireless* linkWireless;

#endif  // LINK_WIRELESS_H
