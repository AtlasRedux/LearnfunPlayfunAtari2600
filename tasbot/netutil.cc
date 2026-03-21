
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "netutil.h"

using namespace std;

string PeerString(SOCKET sock) {
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  if (getpeername(sock, (struct sockaddr *)&addr, &addrlen) == 0) {
    char host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host));
    return StringPrintf("%s:%d", host, ntohs(addr.sin_port));
  }
  return "<unknown>";
}

SOCKET ConnectLocal(int port) {
  struct addrinfo hints = {}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char portStr[16];
  snprintf(portStr, sizeof(portStr), "%d", port);

  if (getaddrinfo("localhost", portStr, &hints, &result) != 0) {
    fprintf(stderr, "getaddrinfo failed: %d\n", WSAGetLastError());
    abort();
  }

  SOCKET sock = socket(result->ai_family, result->ai_socktype,
                       result->ai_protocol);
  if (sock == INVALID_SOCKET) {
    freeaddrinfo(result);
    fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
    abort();
  }

  if (connect(sock, result->ai_addr, (int)result->ai_addrlen)
      == SOCKET_ERROR) {
    fprintf(stderr, "connect(localhost:%d) failed: %d\n",
            port, WSAGetLastError());
    closesocket(sock);
    freeaddrinfo(result);
    abort();
  }

  freeaddrinfo(result);
  return sock;
}

SOCKET TryConnectLocal(int port) {
  struct addrinfo hints = {}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  char portStr[16];
  snprintf(portStr, sizeof(portStr), "%d", port);

  if (getaddrinfo("localhost", portStr, &hints, &result) != 0) {
    return INVALID_SOCKET;
  }

  SOCKET sock = socket(result->ai_family, result->ai_socktype,
                       result->ai_protocol);
  if (sock == INVALID_SOCKET) {
    freeaddrinfo(result);
    return INVALID_SOCKET;
  }

  if (connect(sock, result->ai_addr, (int)result->ai_addrlen)
      == SOCKET_ERROR) {
    closesocket(sock);
    freeaddrinfo(result);
    return INVALID_SOCKET;
  }

  freeaddrinfo(result);
  return sock;
}

void BlockOnSocket(SOCKET sock) {
  fd_set readfds;
  for (;;) {
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    // Infinite timeout (NULL timeval).
    // On Windows, first arg to select() is ignored.
    int result = select(0, &readfds, nullptr, nullptr, nullptr);
    if (result == SOCKET_ERROR) {
      fprintf(stderr, "select() failed: %d\n", WSAGetLastError());
      abort();
    }
    if (result > 0) break;
  }
}

SingleServer::SingleServer(int port)
  : port_(port), server_(INVALID_SOCKET),
    state_(LISTENING), peer_(INVALID_SOCKET) {
  struct addrinfo hints = {}, *result = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  char portStr[16];
  snprintf(portStr, sizeof(portStr), "%d", port);

  if (getaddrinfo(nullptr, portStr, &hints, &result) != 0) {
    fprintf(stderr, "getaddrinfo failed: %d\n", WSAGetLastError());
    abort();
  }

  server_ = socket(result->ai_family, result->ai_socktype,
                   result->ai_protocol);
  CHECK(server_ != INVALID_SOCKET);

  // Allow address reuse.
  int opt = 1;
  setsockopt(server_, SOL_SOCKET, SO_REUSEADDR,
             (const char *)&opt, sizeof(opt));

  if (::bind(server_, result->ai_addr, (int)result->ai_addrlen)
      == SOCKET_ERROR) {
    fprintf(stderr, "bind(port %d) failed: %d\n", port, WSAGetLastError());
    freeaddrinfo(result);
    abort();
  }
  freeaddrinfo(result);

  if (listen(server_, SOMAXCONN) == SOCKET_ERROR) {
    fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
    abort();
  }
}

SingleServer::~SingleServer() {
  if (peer_ != INVALID_SOCKET) {
    closesocket(peer_);
  }
  if (server_ != INVALID_SOCKET) {
    closesocket(server_);
  }
}

void SingleServer::Listen() {
  CHECK(state_ == LISTENING);

  for (;;) {
    BlockOnSocket(server_);
    peer_ = accept(server_, nullptr, nullptr);
    if (peer_ != INVALID_SOCKET) {
      state_ = ACTIVE;
      return;
    }

    fprintf(stderr, "Socket was ready but couldn't accept?\n");
    Sleep(1000);
  }
}

string SingleServer::PeerString() {
  CHECK(state_ == ACTIVE);
  return ::PeerString(peer_);
}

void SingleServer::Hangup() {
  if (state_ == ACTIVE) {
    closesocket(peer_);
    peer_ = INVALID_SOCKET;
  }

  state_ = LISTENING;
}

int RecvAll(SOCKET sock, void *buffer, int len) {
  int totalRead = 0;
  char *buf = (char *)buffer;
  while (len > 0) {
    int ret = recv(sock, buf, len, 0);
    if (ret <= 0) return (totalRead > 0) ? totalRead : ret;
    totalRead += ret;
    buf += ret;
    len -= ret;
  }
  return totalRead;
}

RequestCache::RequestCache(int size) : size(size), num(0) {}

// --- HelperPool ---

HelperPool::HelperPool(const vector<int> &ports) {
  for (int p : ports) {
    HelperConn h;
    h.port = p;
    h.sock = INVALID_SOCKET;
    h.connected = false;
    helpers_.push_back(h);
  }
}

HelperPool::~HelperPool() {
  for (auto &h : helpers_) {
    if (h.sock != INVALID_SOCKET) {
      closesocket(h.sock);
      h.sock = INVALID_SOCKET;
    }
  }
}

SOCKET HelperPool::GetSocket(int idx) {
  CHECK(idx >= 0 && idx < (int)helpers_.size());
  HelperConn &h = helpers_[idx];
  if (!h.connected || h.sock == INVALID_SOCKET) {
    if (h.sock != INVALID_SOCKET) {
      closesocket(h.sock);
      h.sock = INVALID_SOCKET;
    }
    // Retry with backoff for auto-spawned helpers that may still be starting.
    for (int attempt = 0; attempt < 20; attempt++) {
      h.sock = TryConnectLocal(h.port);
      if (h.sock != INVALID_SOCKET) break;
      fprintf(stderr, "HelperPool: connect to port %d failed (attempt %d), "
              "retrying...\n", h.port, attempt + 1);
      Sleep(500 * (attempt < 5 ? 1 : 2));
    }
    if (h.sock == INVALID_SOCKET) {
      fprintf(stderr, "HelperPool: Failed to connect to port %d "
              "after retries.\n", h.port);
      abort();
    }
    // TCP_NODELAY: disable Nagle for request-response latency.
    int flag = 1;
    setsockopt(h.sock, IPPROTO_TCP, TCP_NODELAY,
               (const char *)&flag, sizeof(flag));
    h.connected = true;
  }
  return h.sock;
}

void HelperPool::ReturnSocket(int idx, SOCKET sock) {
  CHECK(idx >= 0 && idx < (int)helpers_.size());
  helpers_[idx].sock = sock;
  helpers_[idx].connected = true;
}

void HelperPool::MarkFailed(int idx) {
  CHECK(idx >= 0 && idx < (int)helpers_.size());
  if (helpers_[idx].sock != INVALID_SOCKET) {
    closesocket(helpers_[idx].sock);
  }
  helpers_[idx].sock = INVALID_SOCKET;
  helpers_[idx].connected = false;
}
