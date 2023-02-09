#ifndef LINK_WIRELESS_H
#define LINK_WIRELESS_H

// --------------------------------------------------------------------------
// A high level driver for the GBA Wireless Adapter.
// --------------------------------------------------------------------------
// Usage:
// - 1) Include this header in your main.cpp file and add:
//       LinkWireless* linkWireless = new LinkWireless();
// - 2) Add the required interrupt service routines: (*)
//       irq_init(NULL);
//       irq_add(II_VBLANK, LINK_WIRELESS_ISR_VBLANK);
//       irq_add(II_SERIAL, LINK_WIRELESS_ISR_SERIAL);
//       irq_add(II_TIMER3, LINK_WIRELESS_ISR_TIMER);
// - 3) Initialize the library with:
//       linkWireless->activate();
// - 4) Start a server:
//       linkWireless->serve();
//
//       // `getState()` should return SERVING now...
//       // `currentPlayerId()` should return 0
//       // `playerCount()` should return the number of active consoles
// - 5) Connect to a server:
//       std::vector<LinkWireless::Server> servers;
//       linkWireless->getServers(servers);
//       if (servers.empty()) return;
//
//       linkWireless->connect(servers[0].id);
//       while (linkWireless->getState() == LinkWireless::State::CONNECTING)
//         linkWireless->keepConnecting();
//
//       // `getState()` should return CONNECTED now...
//       // `currentPlayerId()` should return 1, 2, 3, or 4 (the host is 0)
//       // `playerCount()` should return the number of active consoles
// - 6) Send data:
//       linkConnection->send(std::vector<u32>{1, 2, 3});
// - 7) Receive data:
//       auto messages = linkConnection->receive();
//       if (messages.size() > 0) {
//         // ...
//       }
// - 8) Disconnect:
//       linkWireless->activate();
//       // (resets the adapter)
// --------------------------------------------------------------------------
// (*) libtonc's interrupt handler sometimes ignores interrupts due to a bug.
//     That can cause packet loss. You might want to use libugba's instead.
//     (see examples)
// --------------------------------------------------------------------------
// `send(...)` restrictions:
// - servers can send up to 19 words of 32 bits at a time!
// - clients can send up to 3 words of 32 bits at a time!
// - if retransmission is on, these limits drop to 14 and 1!
// - don't send 0xFFFFFFFF, it's reserved for errors!
// --------------------------------------------------------------------------

#include <tonc_core.h>
#include <algorithm>
#include <string>
#include <vector>
#include "LinkGPIO.h"
#include "LinkSPI.h"

#define LINK_WIRELESS_MAX_PLAYERS 5
#define LINK_WIRELESS_MIN_PLAYERS 2
#define LINK_WIRELESS_DEFAULT_TIMEOUT 5
#define LINK_WIRELESS_DEFAULT_REMOTE_TIMEOUT 5
#define LINK_WIRELESS_DEFAULT_BUFFER_SIZE 30
#define LINK_WIRELESS_DEFAULT_INTERVAL 50
#define LINK_WIRELESS_DEFAULT_SEND_TIMER_ID 3
#define LINK_WIRELESS_BASE_FREQUENCY TM_FREQ_1024
#define LINK_WIRELESS_MSG_CONFIRMATION 0
#define LINK_WIRELESS_PING_WAIT 50
#define LINK_WIRELESS_TRANSFER_WAIT 15
#define LINK_WIRELESS_BROADCAST_SEARCH_WAIT_FRAMES 60
#define LINK_WIRELESS_CMD_TIMEOUT 100
#define LINK_WIRELESS_MAX_GAME_NAME_LENGTH 14
#define LINK_WIRELESS_MAX_USER_NAME_LENGTH 8
#define LINK_WIRELESS_MAX_SERVER_TRANSFER_LENGTH 20
#define LINK_WIRELESS_MAX_CLIENT_TRANSFER_LENGTH 4
#define LINK_WIRELESS_LOGIN_STEPS 9
#define LINK_WIRELESS_COMMAND_HEADER 0x9966
#define LINK_WIRELESS_RESPONSE_ACK 0x80
#define LINK_WIRELESS_DATA_REQUEST 0x80000000
#define LINK_WIRELESS_SETUP_MAGIC 0x003c0420
#define LINK_WIRELESS_STILL_CONNECTING 0x01000000
#define LINK_WIRELESS_BROADCAST_LENGTH 6
#define LINK_WIRELESS_BROADCAST_RESPONSE_LENGTH \
  (1 + LINK_WIRELESS_BROADCAST_LENGTH)
#define LINK_WIRELESS_COMMAND_HELLO 0x10
#define LINK_WIRELESS_COMMAND_SETUP 0x17
#define LINK_WIRELESS_COMMAND_BROADCAST 0x16
#define LINK_WIRELESS_COMMAND_START_HOST 0x19
#define LINK_WIRELESS_COMMAND_ACCEPT_CONNECTIONS 0x1a
#define LINK_WIRELESS_COMMAND_BROADCAST_READ_START 0x1c
#define LINK_WIRELESS_COMMAND_BROADCAST_READ_POLL 0x1d
#define LINK_WIRELESS_COMMAND_BROADCAST_READ_END 0x1e
#define LINK_WIRELESS_COMMAND_CONNECT 0x1f
#define LINK_WIRELESS_COMMAND_IS_FINISHED_CONNECT 0x20
#define LINK_WIRELESS_COMMAND_FINISH_CONNECTION 0x21
#define LINK_WIRELESS_COMMAND_SEND_DATA 0x24
#define LINK_WIRELESS_COMMAND_RECEIVE_DATA 0x26
#define LINK_WIRELESS_BARRIER asm volatile("" ::: "memory")

#define LINK_WIRELESS_RESET_IF_NEEDED \
  if (!isEnabled)                     \
    return false;                     \
  if (state == NEEDS_RESET)           \
    if (!reset())                     \
      return false;

static volatile char LINK_WIRELESS_VERSION[] = "LinkWireless/v4.3.0";

void LINK_WIRELESS_ISR_VBLANK();
void LINK_WIRELESS_ISR_SERIAL();
void LINK_WIRELESS_ISR_TIMER();
const u16 LINK_WIRELESS_LOGIN_PARTS[] = {0x494e, 0x494e, 0x544e, 0x544e, 0x4e45,
                                         0x4e45, 0x4f44, 0x4f44, 0x8001};
const u16 LINK_WIRELESS_USER_MAX_SERVER_TRANSFER_LENGTHS[] = {19, 14};
const u32 LINK_WIRELESS_USER_MAX_CLIENT_TRANSFER_LENGTHS[] = {3, 1};
const u16 LINK_WIRELESS_TIMER_IRQ_IDS[] = {IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2,
                                           IRQ_TIMER3};

class LinkWireless {
 public:
  enum State {
    NEEDS_RESET,
    AUTHENTICATED,
    SEARCHING,
    SERVING,
    CONNECTING,
    CONNECTED
  };

  enum Error {
    // User errors
    NONE = 0,
    WRONG_STATE = 1,
    GAME_NAME_TOO_LONG = 2,
    USER_NAME_TOO_LONG = 3,
    INVALID_SEND_SIZE = 4,
    BUFFER_IS_FULL = 5,
    // Communication errors
    COMMAND_FAILED = 6,
    WEIRD_PLAYER_ID = 7,
    SEND_DATA_FAILED = 8,
    RECEIVE_DATA_FAILED = 9,
    BAD_CONFIRMATION = 10,
    BAD_MESSAGE = 11,
    ACKNOWLEDGE_FAILED = 12,
    TIMEOUT = 13,
    REMOTE_TIMEOUT = 14
  };

  struct Message {
    u8 playerId = 0;
    std::vector<u32> data = std::vector<u32>{};

    u32 _packetId = 0;
  };

  struct Server {
    u16 id;
    std::string gameName;
    std::string userName;
  };

  explicit LinkWireless(
      bool forwarding = true,
      bool retransmission = true,
      u8 maxPlayers = LINK_WIRELESS_MAX_PLAYERS,
      u32 timeout = LINK_WIRELESS_DEFAULT_TIMEOUT,
      u32 remoteTimeout = LINK_WIRELESS_DEFAULT_REMOTE_TIMEOUT,
      u32 bufferSize = LINK_WIRELESS_DEFAULT_BUFFER_SIZE,
      u16 interval = LINK_WIRELESS_DEFAULT_INTERVAL,
      u8 sendTimerId = LINK_WIRELESS_DEFAULT_SEND_TIMER_ID) {
    this->config.forwarding = forwarding;
    this->config.retransmission = retransmission;
    this->config.maxPlayers = maxPlayers;
    this->config.timeout = timeout;
    this->config.remoteTimeout = remoteTimeout;
    this->config.bufferSize = bufferSize;
    this->config.interval = interval;
    this->config.sendTimerId = LINK_WIRELESS_DEFAULT_SEND_TIMER_ID;
  }

  bool isActive() { return isEnabled; }

  bool activate() {
    lastError = NONE;
    isEnabled = false;

    LINK_WIRELESS_BARRIER;
    bool success = reset();
    LINK_WIRELESS_BARRIER;

    isEnabled = true;
    return success;
  }

  void deactivate() {
    lastError = NONE;
    isEnabled = false;
    isSessionStateReady = false;
    isSessionStateConsumed = false;
    isResetting = false;
    resetState();
    stop();
  }

  bool serve(std::string gameName = "", std::string userName = "") {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != AUTHENTICATED) {
      lastError = WRONG_STATE;
      return false;
    }
    if (gameName.length() > LINK_WIRELESS_MAX_GAME_NAME_LENGTH) {
      lastError = GAME_NAME_TOO_LONG;
      return false;
    }
    if (userName.length() > LINK_WIRELESS_MAX_GAME_NAME_LENGTH) {
      lastError = USER_NAME_TOO_LONG;
      return false;
    }
    gameName.append(LINK_WIRELESS_MAX_GAME_NAME_LENGTH - gameName.length(), 0);
    userName.append(LINK_WIRELESS_MAX_USER_NAME_LENGTH - userName.length(), 0);

    auto broadcast = std::vector<u32>{
        buildU32(buildU16(gameName[1], gameName[0]), buildU16(0x02, 0x02)),
        buildU32(buildU16(gameName[5], gameName[4]),
                 buildU16(gameName[3], gameName[2])),
        buildU32(buildU16(gameName[9], gameName[8]),
                 buildU16(gameName[7], gameName[6])),
        buildU32(buildU16(gameName[13], gameName[12]),
                 buildU16(gameName[11], gameName[10])),
        buildU32(buildU16(userName[3], userName[2]),
                 buildU16(userName[1], userName[0])),
        buildU32(buildU16(userName[7], userName[6]),
                 buildU16(userName[5], userName[4]))};

    bool success =
        sendCommand(LINK_WIRELESS_COMMAND_BROADCAST, broadcast).success &&
        sendCommand(LINK_WIRELESS_COMMAND_START_HOST).success;

    if (!success) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    isSessionStateReady = false;
    isSessionStateConsumed = false;
    wait(LINK_WIRELESS_TRANSFER_WAIT);
    state = SERVING;

    return true;
  }

  bool getServers(std::vector<Server>& servers) {
    return getServers(servers, []() {});
  }

  template <typename F>
  bool getServers(std::vector<Server>& servers, F onWait) {
    if (!getServersAsyncStart())
      return false;

    waitVBlanks(LINK_WIRELESS_BROADCAST_SEARCH_WAIT_FRAMES, onWait);

    if (!getServersAsyncEnd(servers))
      return false;

    return true;
  }

  bool getServersAsyncStart() {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != AUTHENTICATED) {
      lastError = WRONG_STATE;
      return false;
    }

    bool success =
        sendCommand(LINK_WIRELESS_COMMAND_BROADCAST_READ_START).success;

    if (!success) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    state = SEARCHING;

    return true;
  }

  bool getServersAsyncEnd(std::vector<Server>& servers) {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != SEARCHING) {
      lastError = WRONG_STATE;
      return false;
    }

    auto result = sendCommand(LINK_WIRELESS_COMMAND_BROADCAST_READ_POLL);
    bool success1 =
        result.success &&
        result.responses.size() % LINK_WIRELESS_BROADCAST_RESPONSE_LENGTH == 0;

    if (!success1) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    bool success2 =
        sendCommand(LINK_WIRELESS_COMMAND_BROADCAST_READ_END).success;

    if (!success2) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    u32 totalBroadcasts =
        result.responses.size() / LINK_WIRELESS_BROADCAST_RESPONSE_LENGTH;

    for (u32 i = 0; i < totalBroadcasts; i++) {
      u32 start = LINK_WIRELESS_BROADCAST_RESPONSE_LENGTH * i;

      Server server;
      server.id = (u16)result.responses[start];
      recoverName(server.gameName, result.responses[start + 1], false);
      recoverName(server.gameName, result.responses[start + 2]);
      recoverName(server.gameName, result.responses[start + 3]);
      recoverName(server.gameName, result.responses[start + 4]);
      recoverName(server.userName, result.responses[start + 5]);
      recoverName(server.userName, result.responses[start + 6]);

      servers.push_back(server);
    }

    state = AUTHENTICATED;

    return true;
  }

  bool connect(u16 serverId) {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != AUTHENTICATED) {
      lastError = WRONG_STATE;
      return false;
    }

    bool success =
        sendCommand(LINK_WIRELESS_COMMAND_CONNECT, std::vector<u32>{serverId})
            .success;

    if (!success) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    state = CONNECTING;

    return true;
  }

  bool keepConnecting() {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != CONNECTING) {
      lastError = WRONG_STATE;
      return false;
    }

    auto result1 = sendCommand(LINK_WIRELESS_COMMAND_IS_FINISHED_CONNECT);
    if (!result1.success || result1.responses.size() == 0) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    if (result1.responses[0] == LINK_WIRELESS_STILL_CONNECTING)
      return true;

    u8 assignedPlayerId = 1 + (u8)msB32(result1.responses[0]);
    u16 assignedClientId = (u16)result1.responses[0];

    if (assignedPlayerId >= LINK_WIRELESS_MAX_PLAYERS) {
      reset();
      lastError = WEIRD_PLAYER_ID;
      return false;
    }

    auto result2 = sendCommand(LINK_WIRELESS_COMMAND_FINISH_CONNECTION);
    if (!result2.success || result2.responses.size() == 0 ||
        (u16)result2.responses[0] != assignedClientId) {
      reset();
      lastError = COMMAND_FAILED;
      return false;
    }

    sessionState.currentPlayerId = assignedPlayerId;
    isSessionStateReady = false;
    isSessionStateConsumed = false;
    state = CONNECTED;

    return true;
  }

  bool send(std::vector<u32> data, int _author = -1) {
    LINK_WIRELESS_RESET_IF_NEEDED
    if (state != SERVING && state != CONNECTED) {
      lastError = WRONG_STATE;
      return false;
    }
    u32 maxTransferLength = state == SERVING
                                ? LINK_WIRELESS_USER_MAX_SERVER_TRANSFER_LENGTHS
                                      [config.retransmission]
                                : LINK_WIRELESS_USER_MAX_CLIENT_TRANSFER_LENGTHS
                                      [config.retransmission];
    if (data.size() == 0 || data.size() > maxTransferLength) {
      lastError = INVALID_SEND_SIZE;
      return false;
    }

    if (_sessionState.outgoingMessages.size() >= config.bufferSize) {
      lastError = BUFFER_IS_FULL;
      return false;
    }

    LINK_WIRELESS_BARRIER;
    isAddingMessage = true;
    LINK_WIRELESS_BARRIER;

    Message message;
    message.playerId = _author < 0 ? sessionState.currentPlayerId : _author;
    message.data = data;
    message._packetId = ++_sessionState.lastPacketId;

    _sessionState.outgoingMessages.push_back(message);

    LINK_WIRELESS_BARRIER;
    isAddingMessage = false;
    LINK_WIRELESS_BARRIER;

    if (isResetting) {
      _sessionState.outgoingMessages.clear();
      isResetting = false;
    }

    return true;
  }

  std::vector<Message> receive() {
    if (!isEnabled || state == NEEDS_RESET ||
        (state != SERVING && state != CONNECTED) ||
        (!isSessionStateReady || isSessionStateConsumed))
      return std::vector<Message>{};

    LINK_WIRELESS_BARRIER;
    auto messages = $sessionState.incomingMessages;
    LINK_WIRELESS_BARRIER;
    isSessionStateConsumed = true;
    LINK_WIRELESS_BARRIER;

    return messages;
  }

  State getState() { return state; }
  bool isConnected() { return $sessionState.playerCount > 1; }
  u8 playerCount() { return $sessionState.playerCount; }
  u8 currentPlayerId() { return $sessionState.currentPlayerId; }

  bool canSend() {
    return _sessionState.outgoingMessages.size() < config.bufferSize;
  }

  u32 getPendingCount() { return _sessionState.outgoingMessages.size(); }
  Error getLastError() {
    Error error = lastError;
    lastError = NONE;
    return error;
  }

  ~LinkWireless() {
    delete linkSPI;
    delete linkGPIO;
  }

  void _onVBlank() {
    if (!isEnabled)
      return;

    if (state != SERVING && state != CONNECTED) {
      copyState();
      return;
    }

    if (_isConnected() && _sessionState.frameRecvCount == 0)
      _sessionState.recvTimeout++;

    _sessionState.frameRecvCount = 0;
    _sessionState.acceptCalled = false;

    copyState();
  }

  void _onSerial() {
    if (!isEnabled)
      return;

    linkSPI->_onSerial(true);

    bool hasNewData = linkSPI->getAsyncState() == LinkSPI::AsyncState::READY;
    if (hasNewData)
      if (!acknowledge()) {
        reset();
        lastError = ACKNOWLEDGE_FAILED;
        copyState();
        return;
      }
    u32 newData = linkSPI->getAsyncData();

    if (state != SERVING && state != CONNECTED) {
      copyState();
      return;
    }

    if (asyncCommand.isActive) {
      if (asyncCommand.state == AsyncCommand::State::PENDING) {
        if (hasNewData)
          updateAsyncCommand(newData);
        else
          asyncCommand.state = AsyncCommand::State::COMPLETED;

        if (asyncCommand.state == AsyncCommand::State::COMPLETED)
          processAsyncCommand();
      }
    }

    copyState();
  }

  void _onTimer() {
    if (!isEnabled)
      return;

    if (state != SERVING && state != CONNECTED) {
      copyState();
      return;
    }

    if (_sessionState.recvTimeout >= config.timeout) {
      reset();
      lastError = TIMEOUT;
      copyState();
      return;
    }

    if (!asyncCommand.isActive)
      acceptConnectionsOrSendData();

    copyState();
  }

 private:
  struct Config {
    bool forwarding;
    bool retransmission;
    u8 maxPlayers;
    u32 timeout;
    u32 remoteTimeout;
    u32 bufferSize;
    u32 interval;
    u32 sendTimerId;
  };

  struct ExternalSessionState {
    std::vector<Message> incomingMessages;
    u8 playerCount = 1;
    u8 currentPlayerId = 0;
  };

  struct InternalSessionState {
    std::vector<Message> outgoingMessages;
    u32 timeouts[LINK_WIRELESS_MAX_PLAYERS];
    u32 recvTimeout = 0;
    u32 frameRecvCount = 0;
    bool acceptCalled = false;

    u32 lastPacketId = 0;
    u32 lastPacketIdFromServer = 0;
    u32 lastConfirmationFromServer = 0;
    u32 lastPacketIdFromClients[LINK_WIRELESS_MAX_PLAYERS];
    u32 lastConfirmationFromClients[LINK_WIRELESS_MAX_PLAYERS];
  };

  struct LoginMemory {
    u16 previousGBAData = 0xffff;
    u16 previousAdapterData = 0xffff;
  };

  struct CommandResult {
    bool success = false;
    std::vector<u32> responses = std::vector<u32>{};
  };

  struct MessageHeader {
    unsigned int packetId : 22;
    unsigned int size : 5;
    unsigned int playerId : 3;
    unsigned int clientCount : 2;
  };

  union MessageHeaderSerializer {
    MessageHeader asStruct;
    u32 asInt;
  };

  struct AsyncCommand {
    enum State { PENDING, COMPLETED };

    enum Step {
      COMMAND_HEADER,
      COMMAND_PARAMETERS,
      RESPONSE_REQUEST,
      DATA_REQUEST
    };

    u8 type;
    std::vector<u32> parameters;
    CommandResult result;
    State state;
    Step step;
    u32 sentParameters, totalParameters;
    u32 receivedResponses, totalResponses;
    bool isActive;
  };

  ExternalSessionState sessionState;   // (updated state / back buffer)
  ExternalSessionState $sessionState;  // (visible state / front buffer)
  InternalSessionState _sessionState;  // (internal state)
  AsyncCommand asyncCommand;           // (current command)
  Config config;
  LinkSPI* linkSPI = new LinkSPI();
  LinkGPIO* linkGPIO = new LinkGPIO();
  State state = NEEDS_RESET;
  bool isSessionStateReady = false;
  bool isSessionStateConsumed = false;
  bool isAddingMessage = false;
  bool isResetting = false;
  Error lastError = NONE;
  bool isEnabled = false;

  void processAsyncCommand() {
    if (!asyncCommand.result.success) {
      if (asyncCommand.type == LINK_WIRELESS_COMMAND_SEND_DATA)
        lastError = SEND_DATA_FAILED;
      else if (asyncCommand.type == LINK_WIRELESS_COMMAND_RECEIVE_DATA)
        lastError = RECEIVE_DATA_FAILED;
      else
        lastError = COMMAND_FAILED;

      reset();
      return;
    }

    asyncCommand.isActive = false;

    switch (asyncCommand.type) {
      case LINK_WIRELESS_COMMAND_ACCEPT_CONNECTIONS: {
        // Accept connections (end)
        sessionState.playerCount = 1 + asyncCommand.result.responses.size();

        break;
      }
      case LINK_WIRELESS_COMMAND_SEND_DATA: {
        // Send data (end)
        if (!config.retransmission)
          _sessionState.outgoingMessages.clear();

        // Receive data (start)
        sendCommandAsync(LINK_WIRELESS_COMMAND_RECEIVE_DATA);

        break;
      }
      case LINK_WIRELESS_COMMAND_RECEIVE_DATA: {
        // Receive data (end)
        if (!asyncCommand.result.responses.empty()) {
          _sessionState.frameRecvCount++;
          _sessionState.recvTimeout = 0;

          // (remove wireless header)
          asyncCommand.result.responses.erase(
              asyncCommand.result.responses.begin());
        }

        trackRemoteTimeouts();

        std::vector<Message> messages;
        translateDataIntoMessages(asyncCommand.result.responses, messages);
        sessionState.incomingMessages.insert(
            sessionState.incomingMessages.end(), messages.begin(),
            messages.end());

        if (!checkRemoteTimeouts()) {
          reset();
          lastError = REMOTE_TIMEOUT;
          return;
        }

        break;
      }
      default: {
      }
    }
  }

  void acceptConnectionsOrSendData() {
    if (state == SERVING && !_sessionState.acceptCalled &&
        sessionState.playerCount < config.maxPlayers) {
      // Accept connections (start)
      sendCommandAsync(LINK_WIRELESS_COMMAND_ACCEPT_CONNECTIONS);
      _sessionState.acceptCalled = true;
    } else if (_isConnected()) {
      // Send data (start)
      sendPendingData();
    }
  }

  void sendPendingData() {
    if (isAddingMessage)
      return;

    LINK_WIRELESS_BARRIER;

    addPingMessageIfNeeded();
    auto data = translateMessagesIntoData();
    sendCommandAsync(LINK_WIRELESS_COMMAND_SEND_DATA, data);
  }

  void addPingMessageIfNeeded() {
    if (_sessionState.outgoingMessages.empty() && !config.retransmission) {
      Message emptyMessage;
      emptyMessage.playerId = sessionState.currentPlayerId;
      emptyMessage._packetId = ++_sessionState.lastPacketId;
      _sessionState.outgoingMessages.push_back(emptyMessage);
    }
  }

  std::vector<u32> translateMessagesIntoData() {
    u32 maxTransferLength = getDeviceTransferLength();
    std::vector<u32> data;

    // (add wireless header)
    data.push_back(0);

    if (config.retransmission)
      addConfirmations(data);

    for (auto& message : _sessionState.outgoingMessages) {
      u8 size = message.data.size();
      u32 header =
          buildMessageHeader(message.playerId, size, message._packetId);

      if (data.size() + 1 + size > maxTransferLength)
        break;

      data.push_back(header);
      data.insert(data.end(), message.data.begin(), message.data.end());
    }

    u32 bytes = (data.size() - 1) * 4;
    data[0] = sessionState.currentPlayerId == 0
                  ? bytes
                  : (1 << (3 + sessionState.currentPlayerId * 5)) * bytes;

    return data;
  }

  bool translateDataIntoMessages(std::vector<u32>& data,
                                 std::vector<Message>& messages) {
    for (u32 i = 0; i < data.size(); i++) {
      MessageHeaderSerializer serializer;
      serializer.asInt = data[i];

      MessageHeader header = serializer.asStruct;
      u8 remotePlayerCount = LINK_WIRELESS_MIN_PLAYERS + header.clientCount;
      u8 remotePlayerId = header.playerId;
      u8 size = header.size;
      u32 packetId = header.packetId;

      if (i + size >= data.size()) {
        reset();
        lastError = BAD_MESSAGE;
        return false;
      }

      _sessionState.timeouts[0] = 0;
      _sessionState.timeouts[remotePlayerId] = 0;

      if (state == SERVING) {
        if (config.retransmission &&
            packetId != LINK_WIRELESS_MSG_CONFIRMATION &&
            _sessionState.lastPacketIdFromClients[remotePlayerId] > 0 &&
            packetId !=
                _sessionState.lastPacketIdFromClients[remotePlayerId] + 1)
          goto skip;

        if (packetId != LINK_WIRELESS_MSG_CONFIRMATION)
          _sessionState.lastPacketIdFromClients[remotePlayerId] = packetId;
      } else {
        if (config.retransmission &&
            packetId != LINK_WIRELESS_MSG_CONFIRMATION &&
            _sessionState.lastPacketIdFromServer > 0 &&
            packetId != _sessionState.lastPacketIdFromServer + 1)
          goto skip;

        sessionState.playerCount = remotePlayerCount;

        if (packetId != LINK_WIRELESS_MSG_CONFIRMATION)
          _sessionState.lastPacketIdFromServer = packetId;
      }

      if (remotePlayerId == sessionState.currentPlayerId) {
      skip:
        i += size;
        continue;
      }

      if (size > 0) {
        Message message;
        message.playerId = remotePlayerId;
        for (u32 j = 0; j < size; j++)
          message.data.push_back(data[i + 1 + j]);
        message._packetId = packetId;

        if (config.retransmission &&
            packetId == LINK_WIRELESS_MSG_CONFIRMATION) {
          if (!handleConfirmation(message)) {
            reset();
            lastError = BAD_CONFIRMATION;
            return false;
          }
        } else {
          messages.push_back(message);
        }

        i += size;
      }
    }

    return true;
  }

  void forwardMessagesIfNeeded(std::vector<Message>& messages, u32 startIndex) {
    if (state == SERVING && config.forwarding && sessionState.playerCount > 2) {
      for (u32 i = startIndex; i < messages.size(); i++) {
        auto message = messages[i];
        send(message.data, message.playerId);
      }
    }
  }

  void addConfirmations(std::vector<u32>& data) {
    if (state == SERVING) {
      data.push_back(buildConfirmationHeader(0));
      for (u32 i = 0; i < LINK_WIRELESS_MAX_PLAYERS - 1; i++)
        data.push_back(_sessionState.lastPacketIdFromClients[1 + i]);
    } else {
      data.push_back(buildConfirmationHeader(sessionState.currentPlayerId));
      data.push_back(_sessionState.lastPacketIdFromServer);
    }
  }

  bool handleConfirmation(Message confirmation) {
    if (confirmation.data.size() == 0)
      return false;

    bool isServerConfirmation = confirmation.playerId == 0;

    if (isServerConfirmation) {
      if (state != CONNECTED ||
          confirmation.data.size() != LINK_WIRELESS_MAX_PLAYERS - 1)
        return false;

      _sessionState.lastConfirmationFromServer =
          confirmation.data[sessionState.currentPlayerId - 1];
      removeConfirmedMessages(_sessionState.lastConfirmationFromServer);
    } else {
      if (state != SERVING || confirmation.data.size() != 1)
        return false;

      u32 confirmationData = confirmation.data[0];
      _sessionState.lastConfirmationFromClients[confirmation.playerId] =
          confirmationData;

      u32 min = 0xffffffff;
      for (u32 i = 0; i < LINK_WIRELESS_MAX_PLAYERS - 1; i++) {
        u32 confirmationData = _sessionState.lastConfirmationFromClients[1 + i];
        if (confirmationData > 0 && confirmationData < min)
          min = confirmationData;
      }
      if (min < 0xffffffff)
        removeConfirmedMessages(min);
    }

    return true;
  }

  void removeConfirmedMessages(u32 confirmation) {
    _sessionState.outgoingMessages.erase(
        std::remove_if(_sessionState.outgoingMessages.begin(),
                       _sessionState.outgoingMessages.end(),
                       [confirmation](Message it) {
                         return it._packetId <= confirmation;
                       }),
        _sessionState.outgoingMessages.end());
  }

  u32 buildConfirmationHeader(u8 playerId) {
    return buildMessageHeader(
        playerId, playerId == 0 ? LINK_WIRELESS_MAX_PLAYERS - 1 : 1, 0);
  }

  u32 buildMessageHeader(u8 playerId, u8 size, u32 packetId) {
    MessageHeader header;
    header.clientCount = sessionState.playerCount - LINK_WIRELESS_MIN_PLAYERS;
    header.playerId = playerId;
    header.size = size;
    header.packetId = packetId;

    MessageHeaderSerializer serializer;
    serializer.asStruct = header;
    return serializer.asInt;
  }

  void trackRemoteTimeouts() {
    for (u32 i = 0; i < sessionState.playerCount; i++)
      if (i != sessionState.currentPlayerId)
        _sessionState.timeouts[i]++;
  }

  bool checkRemoteTimeouts() {
    for (u32 i = 0; i < sessionState.playerCount; i++) {
      if ((i == 0 || state == SERVING) &&
          _sessionState.timeouts[i] > config.timeout)
        return false;
    }

    return true;
  }

  u32 getDeviceTransferLength() {
    return state == SERVING ? LINK_WIRELESS_MAX_SERVER_TRANSFER_LENGTH
                            : LINK_WIRELESS_MAX_CLIENT_TRANSFER_LENGTH;
  }

  void recoverName(std::string& name,
                   u32 word,
                   bool includeFirstTwoBytes = true) {
    u32 character = 0;
    if (includeFirstTwoBytes) {
      character = lsB16(lsB32(word));
      if (character > 0)
        name.push_back(character);
      character = msB16(lsB32(word));
      if (character > 0)
        name.push_back(character);
    }
    character = lsB16(msB32(word));
    if (character > 0)
      name.push_back(character);
    character = msB16(msB32(word));
    if (character > 0)
      name.push_back(character);
  }

  bool reset() {
    resetState();
    stop();
    return start();
  }

  void resetState() {
    this->state = NEEDS_RESET;
    this->sessionState.incomingMessages.clear();
    this->sessionState.playerCount = 1;
    this->sessionState.currentPlayerId = 0;

    this->_sessionState.recvTimeout = 0;
    this->_sessionState.frameRecvCount = 0;
    this->_sessionState.acceptCalled = false;
    this->_sessionState.lastPacketId = 0;
    this->_sessionState.lastPacketIdFromServer = 0;
    this->_sessionState.lastConfirmationFromServer = 0;
    for (u32 i = 0; i < LINK_WIRELESS_MAX_PLAYERS; i++) {
      this->_sessionState.timeouts[i] = 0;
      this->_sessionState.lastPacketIdFromClients[i] = 0;
      this->_sessionState.lastConfirmationFromClients[i] = 0;
    }
    this->asyncCommand.isActive = false;

    if (isAddingMessage || isResetting)
      isResetting = true;
    else
      this->_sessionState.outgoingMessages.clear();
  }

  void stop() {
    stopTimer();

    linkSPI->deactivate();
  }

  bool start() {
    startTimer();

    pingAdapter();
    linkSPI->activate(LinkSPI::Mode::MASTER_256KBPS);

    if (!login())
      return false;

    wait(LINK_WIRELESS_TRANSFER_WAIT);

    if (!sendCommand(LINK_WIRELESS_COMMAND_HELLO).success)
      return false;

    if (!sendCommand(LINK_WIRELESS_COMMAND_SETUP,
                     std::vector<u32>{LINK_WIRELESS_SETUP_MAGIC})
             .success)
      return false;

    linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
    state = AUTHENTICATED;

    return true;
  }

  void stopTimer() {
    REG_TM[config.sendTimerId].cnt =
        REG_TM[config.sendTimerId].cnt & (~TM_ENABLE);
  }

  void startTimer() {
    REG_TM[config.sendTimerId].start = -config.interval;
    REG_TM[config.sendTimerId].cnt =
        TM_ENABLE | TM_IRQ | LINK_WIRELESS_BASE_FREQUENCY;
  }

  void copyState() {
    if (isSessionStateReady && !isSessionStateConsumed)
      return;

    LINK_WIRELESS_BARRIER;
    $sessionState.playerCount = sessionState.playerCount;
    $sessionState.currentPlayerId = sessionState.currentPlayerId;
    $sessionState.incomingMessages.swap(sessionState.incomingMessages);
    sessionState.incomingMessages.clear();
    LINK_WIRELESS_BARRIER;
    isSessionStateReady = true;
    isSessionStateConsumed = false;
    LINK_WIRELESS_BARRIER;
  }

  bool _isConnected() { return sessionState.playerCount > 1; }

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

    if (header != LINK_WIRELESS_COMMAND_HEADER)
      return result;
    if (ack != type + LINK_WIRELESS_RESPONSE_ACK)
      return result;

    for (u32 i = 0; i < responses; i++)
      result.responses.push_back(transfer(LINK_WIRELESS_DATA_REQUEST));

    result.success = true;
    return result;
  }

  void sendCommandAsync(u8 type, std::vector<u32> params = std::vector<u32>{}) {
    if (asyncCommand.isActive)
      return;

    asyncCommand.type = type;
    asyncCommand.parameters = params;
    asyncCommand.result.success = false;
    asyncCommand.result.responses = std::vector<u32>{};
    asyncCommand.state = AsyncCommand::State::PENDING;
    asyncCommand.step = AsyncCommand::Step::COMMAND_HEADER;
    asyncCommand.sentParameters = 0;
    asyncCommand.totalParameters = params.size();
    asyncCommand.receivedResponses = 0;
    asyncCommand.totalResponses = 0;
    asyncCommand.isActive = true;

    u32 command = buildCommand(type, asyncCommand.totalParameters);
    transferAsync(command);
  }

  void updateAsyncCommand(u32 newData) {
    switch (asyncCommand.step) {
      case AsyncCommand::Step::COMMAND_HEADER: {
        if (newData != LINK_WIRELESS_DATA_REQUEST) {
          asyncCommand.state = AsyncCommand::State::COMPLETED;
          return;
        }

        sendAsyncCommandParameterOrRequestResponse();
        break;
      }
      case AsyncCommand::Step::COMMAND_PARAMETERS: {
        if (newData != LINK_WIRELESS_DATA_REQUEST) {
          asyncCommand.state = AsyncCommand::State::COMPLETED;
          return;
        }

        sendAsyncCommandParameterOrRequestResponse();
        break;
      }
      case AsyncCommand::Step::RESPONSE_REQUEST: {
        u16 header = msB32(newData);
        u16 data = lsB32(newData);
        u8 responses = msB16(data);
        u8 ack = lsB16(data);

        if (header != LINK_WIRELESS_COMMAND_HEADER ||
            ack != asyncCommand.type + LINK_WIRELESS_RESPONSE_ACK) {
          asyncCommand.state = AsyncCommand::State::COMPLETED;
          return;
        }

        asyncCommand.totalResponses = responses;
        receiveAsyncCommandResponseOrFinish();
        break;
      }
      case AsyncCommand::Step::DATA_REQUEST: {
        asyncCommand.result.responses.push_back(newData);
        receiveAsyncCommandResponseOrFinish();
        break;
      }
    }
  }

  void sendAsyncCommandParameterOrRequestResponse() {
    if (asyncCommand.sentParameters < asyncCommand.totalParameters) {
      asyncCommand.step = AsyncCommand::Step::COMMAND_PARAMETERS;
      transferAsync(asyncCommand.parameters[asyncCommand.sentParameters]);
      asyncCommand.sentParameters++;
    } else {
      asyncCommand.step = AsyncCommand::Step::RESPONSE_REQUEST;
      transferAsync(LINK_WIRELESS_DATA_REQUEST);
    }
  }

  void receiveAsyncCommandResponseOrFinish() {
    if (asyncCommand.receivedResponses < asyncCommand.totalResponses) {
      asyncCommand.step = AsyncCommand::Step::DATA_REQUEST;
      transferAsync(LINK_WIRELESS_DATA_REQUEST);
      asyncCommand.receivedResponses++;
    } else {
      asyncCommand.result.success = true;
      asyncCommand.state = AsyncCommand::State::COMPLETED;
    }
  }

  u32 buildCommand(u8 type, u8 length = 0) {
    return buildU32(LINK_WIRELESS_COMMAND_HEADER, buildU16(length, type));
  }

  void transferAsync(u32 data) {
    linkSPI->transfer(
        data, []() { return false; }, true, true);
  }

  u32 transfer(u32 data, bool customAck = true) {
    if (!customAck)
      wait(LINK_WIRELESS_TRANSFER_WAIT);

    u32 lines = 0;
    u32 vCount = REG_VCOUNT;
    u32 receivedData = linkSPI->transfer(
        data, [this, &lines, &vCount]() { return cmdTimeout(lines, vCount); },
        false, customAck);

    if (customAck && !acknowledge())
      return LINK_SPI_NO_DATA;

    return receivedData;
  }

  bool acknowledge() {
    u32 lines = 0;
    u32 vCount = REG_VCOUNT;

    linkSPI->_setSOLow();
    while (!linkSPI->_isSIHigh())
      if (cmdTimeout(lines, vCount))
        return false;
    linkSPI->_setSOHigh();
    while (linkSPI->_isSIHigh())
      if (cmdTimeout(lines, vCount))
        return false;
    linkSPI->_setSOLow();

    return true;
  }

  bool cmdTimeout(u32& lines, u32& vCount) {
    return timeout(LINK_WIRELESS_CMD_TIMEOUT, lines, vCount);
  }

  bool timeout(u32 limit, u32& lines, u32& vCount) {
    if (REG_VCOUNT != vCount) {
      lines += std::max((s32)REG_VCOUNT - (s32)vCount, 0);
      vCount = REG_VCOUNT;
    }

    return lines > limit;
  }

  void wait(u32 verticalLines) {
    u32 count = 0;
    u32 vCount = REG_VCOUNT;

    while (count < verticalLines) {
      if (REG_VCOUNT != vCount) {
        count += std::max((s32)REG_VCOUNT - (s32)vCount, 0);
        vCount = REG_VCOUNT;
      }
    };
  }

  template <typename F>
  void waitVBlanks(u32 vBlanks, F onVBlank) {
    u32 count = 0;
    u32 vCount = REG_VCOUNT;

    while (count < vBlanks) {
      if (REG_VCOUNT != vCount) {
        vCount = REG_VCOUNT;

        if (vCount == 160) {
          onVBlank();
          count++;
        }
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

inline void LINK_WIRELESS_ISR_VBLANK() {
  linkWireless->_onVBlank();
}

inline void LINK_WIRELESS_ISR_SERIAL() {
  linkWireless->_onSerial();
}

inline void LINK_WIRELESS_ISR_TIMER() {
  linkWireless->_onTimer();
}

#endif  // LINK_WIRELESS_H
