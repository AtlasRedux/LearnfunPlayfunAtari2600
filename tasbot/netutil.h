
#ifndef __TASBOT_NETUTIL_H
#define __TASBOT_NETUTIL_H

#include <vector>
#include <string>
#include <deque>
#include <utility>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "tasbot.h"
#include "marionet.pb.h"
#include "util.h"

// You can change this, but it must be less than 2^32 since
// we only send 4 bytes.
#define MAX_MESSAGE (1<<30)

#if !MARIONET
#error You should only include net utils when MARIONET is defined.
#endif

using namespace std;

// Get a human-readable string for a connected socket's peer address.
extern string PeerString(SOCKET sock);

// Block indefinitely on a socket until activity.
extern void BlockOnSocket(SOCKET sock);

// Connect to localhost at the given port. Blocks. Aborts on failure.
extern SOCKET ConnectLocal(int port);

// Like ConnectLocal but returns INVALID_SOCKET on failure instead of aborting.
extern SOCKET TryConnectLocal(int port);

// Receive exactly len bytes, retrying on partial reads.
// Returns total bytes read, or <= 0 on error.
extern int RecvAll(SOCKET sock, void *buffer, int len);

// Blocks until the entire proto can be read.
// If this returns false, you probably want to close the socket.
template <class T>
bool ReadProto(SOCKET sock, T *t);

// If this returns false, you probably want to close the socket.
template <class T>
bool WriteProto(SOCKET sock, const T &t);

// Listens on a single port for a single connection at a time,
// blocking.
struct SingleServer {
  // Aborts if listening fails.
  explicit SingleServer(int port);
  ~SingleServer();

  // Server starts in LISTENING state.
  enum State {
    LISTENING,
    ACTIVE,
  };

  // Must be in LISTENING state. Blocks until ACTIVE.
  void Listen();

  // Must be in ACTIVE state.
  // On error, returns false and transitions to LISTENING state.
  template <class T>
  bool ReadProto(T *t);

  // Must be in ACTIVE state.
  // On error, returns false and transitions to LISTENING state.
  template <class T>
  bool WriteProto(const T &t);

  // Must be in ACTIVE state; transitions to LISTENING.
  void Hangup();

  // Must be in ACTIVE state.
  string PeerString();

 private:
  const int port_;
  SOCKET server_;
  State state_;
  SOCKET peer_;
};

// Manages persistent TCP connections to helper processes.
// Reuses sockets across multiple GetAnswers invocations to avoid
// the per-request connect/accept/close overhead of the legacy mode.
struct HelperPool {
  explicit HelperPool(const vector<int> &ports);
  ~HelperPool();

  // Get socket for helper idx, reconnecting if needed.
  SOCKET GetSocket(int idx);
  // Return socket after successful use (keeps it for reuse).
  void ReturnSocket(int idx, SOCKET sock);
  // Mark helper as failed; will reconnect on next GetSocket.
  void MarkFailed(int idx);

  int NumHelpers() const { return (int)helpers_.size(); }
  int GetPort(int idx) const { return helpers_[idx].port; }

 private:
  struct HelperConn {
    int port;
    SOCKET sock;
    bool connected;
  };
  vector<HelperConn> helpers_;
};

// Manages multiple outstanding requests to servers (e.g.
// SingleServers, running in other processes.).
template <class Request, class Response>
struct GetAnswers {

  // Legacy constructor: creates new connections each time.
  // Request vector must outlast the object.
  GetAnswers(const vector<int> &ports,
             const vector<Request> &requests)
  : pool_(nullptr),
    workdone_(0),
    workqueued_(0) {

    for (int i = 0; i < (int)ports.size(); i++) {
      helpers_.push_back(Helper(ports[i], i));
    }

    for (int i = 0; i < (int)requests.size(); i++) {
      work_.push_back(Work(&requests[i]));
      done_.push_back(false);
    }
  }

  // Persistent pool constructor: reuses connections across calls.
  GetAnswers(HelperPool *pool,
             const vector<Request> &requests)
  : pool_(pool),
    workdone_(0),
    workqueued_(0) {

    for (int i = 0; i < pool->NumHelpers(); i++) {
      helpers_.push_back(Helper(pool->GetPort(i), i));
    }

    for (int i = 0; i < (int)requests.size(); i++) {
      work_.push_back(Work(&requests[i]));
      done_.push_back(false);
    }
  }

  void Loop() {
    InPlaceTerminal term(1);
    for (;;) {
      static const int MAXCOLS = 77;

      // If we have more tasks than fit on a line,
      // only show the left or right end.
      int low = 0, high = (int)work_.size();
      int overflow = (high - low) - MAXCOLS;
      if (overflow > 0) {
        low = min(overflow, workdone_);
        overflow -= low;
        if (overflow > 0) {
          high -= overflow;
        }
      }
      CHECK(low < high);

      string meter =
        StringPrintf("%c", (low == 0) ? '[' : '<');
      for (int i = low; i < high; i++) {
        if (done_[i]) {
          if (i < workdone_) {
            meter += ANSI_GREY "#" ANSI_RESET;
          } else {
            meter += "#";
          }
        } else if (i < workqueued_) {
          int helper = -1;
          for (int h = 0; h < (int)helpers_.size(); h++) {
            if (helpers_[h].workidx == i) {
              CHECK(helper == -1);
              helper = h;
            }
          }
          CHECK(helper != -1);
          const char c = (helper < 36) ?
            "0123456789abcdefghijklmnopqrstuvwxyz"[helper] : '+';
          meter += StringPrintf(ANSI_CYAN "%c" ANSI_RESET, c);
        } else {
          meter += ".";
        }
      }
      meter += StringPrintf("%c\n", (high == (int)work_.size()) ? ']' : '>');
      term.Output(meter);

      // Are we done?
      if (workdone_ == (int)work_.size()) {
        return;
      }

      // First, see if we can get any more work enqueued.
      while (workqueued_ < (int)work_.size()) {
        int idle = GetIdleHelper();
        if (idle == -1) break;
        DoNextWork(idle);
      }

      // Build fd_set from working helpers.
      fd_set readfds;
      FD_ZERO(&readfds);
      int numworking = 0;
      for (int i = 0; i < (int)helpers_.size(); i++) {
        if (helpers_[i].state == WORKING) {
          numworking++;
          FD_SET(helpers_[i].sock, &readfds);
        }
      }
      if (numworking == 0) {
        // All helpers disconnected — wait and retry.
        fprintf(stderr, "No working helpers — retrying in 2s...\n");
        Sleep(2000);
        // Reset queued work so it gets re-dispatched.
        for (int i = 0; i < (int)work_.size(); i++) {
          if (!done_[i]) {
            // Will be re-queued on next iteration.
          }
        }
        continue;
      }

      // Block with 10-second timeout.
      struct timeval tv;
      tv.tv_sec = 10;
      tv.tv_usec = 0;
      for (;;) {
        fd_set tmpfds = readfds;
        // On Windows, first arg to select() is ignored.
        int numready = select(0, &tmpfds, nullptr, nullptr, &tv);
        if (numready == SOCKET_ERROR) {
          term.Advance();
          fprintf(stderr, "select: %d\n", WSAGetLastError());
          abort();
        }

        if (numready > 0) {
          // Check each working helper.
          for (int i = 0; i < (int)helpers_.size(); i++) {
            Helper *helper = &helpers_[i];

            if (helper->state == WORKING &&
                FD_ISSET(helper->sock, &tmpfds)) {
              int workidx = helper->workidx;
              if (::ReadProto(helper->sock,
                            &work_[workidx].res)) {
                CHECK(done_[workidx] == false);
                done_[workidx] = true;
                if (pool_) {
                  pool_->ReturnSocket(helper->poolidx, helper->sock);
                } else {
                  closesocket(helper->sock);
                }
                helper->sock = INVALID_SOCKET;
                helper->state = DISCONNECTED;
                helper->workidx = -1;
              } else {
                // If we failed to read, reenqueue it in the same
                // helper, which preserves any invariants.
                if (pool_) {
                  pool_->MarkFailed(helper->poolidx);
                } else {
                  closesocket(helper->sock);
                }
                helper->sock = INVALID_SOCKET;
                helper->state = DISCONNECTED;
                term.Advance();
                fprintf(stderr, "Error reading result from port %d "
                        "for work #%d!\n",
                        helper->port,
                        workidx);
                FetchWork(helper, workidx);
              }
            }
          }
          break;
        }
        // timeout -- loop again
      }

      // Advance workdone if we can.
      while (workdone_ < (int)work_.size() && done_[workdone_]) {
        workdone_++;
      }
    }
  }

  struct Work {
    const Request *req;
    Response res;
    explicit Work(const Request *req) : req(req) {}
  };

  const vector<Work> &GetWork() const { return work_; }

 private:
  enum State {
    DISCONNECTED,
    WORKING,
  };

  struct Helper {
    Helper(int port, int poolidx)
    : port(port),
      poolidx(poolidx),
      state(DISCONNECTED),
      workidx(-1),
      sock(INVALID_SOCKET) {}
    int port;
    int poolidx;
    State state;
    int workidx;
    SOCKET sock;
  };

  void FetchWork(Helper *helper, int workidx) {
    CHECK(workidx < workqueued_);
    CHECK(helper->state == DISCONNECTED);
    helper->state = WORKING;
    helper->workidx = workidx;
    if (pool_) {
      helper->sock = pool_->GetSocket(helper->poolidx);
    } else {
      // Retry connection with backoff — helpers may still be starting.
      for (int attempt = 0; attempt < 30; attempt++) {
        helper->sock = TryConnectLocal(helper->port);
        if (helper->sock != INVALID_SOCKET) break;
        if (attempt % 5 == 0) {
          fprintf(stderr, "Waiting for helper on port %d (attempt %d)...\n",
                  helper->port, attempt + 1);
        }
        Sleep(500);
      }
      if (helper->sock == INVALID_SOCKET) {
        fprintf(stderr, "ERROR: Could not connect to helper on port %d "
                "after retries. Is the helper running?\n", helper->port);
        // Mark as disconnected so it can be retried later rather than crashing.
        helper->state = DISCONNECTED;
        helper->workidx = -1;
        return;
      }
    }
    // TCP_NODELAY for lower latency on request-response.
    int flag = 1;
    setsockopt(helper->sock, IPPROTO_TCP, TCP_NODELAY,
               (const char *)&flag, sizeof(flag));
    ::WriteProto(helper->sock, *work_[workidx].req);
  }

  void DoNextWork(int helperidx) {
    CHECK(workqueued_ < (int)work_.size());
    int workidx = workqueued_;
    workqueued_++;
    FetchWork(&helpers_[helperidx], workidx);
  }

  int GetIdleHelper() {
    for (int i = 0; i < (int)helpers_.size(); i++) {
      if (helpers_[i].state == DISCONNECTED) {
        return i;
      }
    }
    return -1;
  }

  HelperPool *pool_;
  vector<Helper> helpers_;
  vector<Work> work_;
  vector<bool> done_;
  int workdone_, workqueued_;
};

// Small exact cache of protos.
struct RequestCache {
  explicit RequestCache(int size);
  typedef ::google::protobuf::Message Message;

  template<class Req, class Res>
  void Save(const Req &request, const Res &response);

  template<class Req>
  const Message *Lookup(const Req &req) const;

 private:
  int size, num;
  deque< pair<string, Message*> > recent;
};

// Template implementations follow.

template<class Req, class Res>
void RequestCache::Save(const Req &request, const Res &response) {
  while (num >= size) {
    delete recent.back().second;
    recent.pop_back();
    num--;
  }

  recent.push_front(make_pair(request.SerializeAsString(),
                              new Res(response)));
  num++;
}

template<class Req>
const RequestCache::Message *RequestCache::Lookup(const Req &req) const {
  string s = req.SerializeAsString();
  for (typename deque< pair<string, Message*> >::const_iterator
         it = recent.begin();
       it != recent.end(); ++it) {
    if (it->first == s) {
      return it->second;
    }
  }
  return NULL;
}

template <class T>
bool ReadProto(SOCKET sock, T *t) {
  CHECK(sock != INVALID_SOCKET);
  CHECK(t != NULL);

  // Read 4-byte length header in network byte order.
  char header[4];
  int bytes = RecvAll(sock, (void *)&header, 4);
  if (4 != bytes) {
    fprintf(stderr, "ReadProto: Failed to read length (got %d), err %d.\n",
            bytes, WSAGetLastError());
    return false;
  }

  uint32_t len = ntohl(*(uint32_t *)&header);
  if (len > MAX_MESSAGE) {
    fprintf(stderr, "Peer sent header with len too big.\n");
    return false;
  }
  char *buffer = (char *)malloc(len);
  CHECK(buffer != NULL);

  int ret = RecvAll(sock, (void *)buffer, len);
  if ((uint32_t)ret != len) {
    fprintf(stderr, "ReadProto: Failed to read %u bytes (got %d), err %d\n",
            (unsigned)len, ret, WSAGetLastError());
    free(buffer);
    return false;
  }

  if (t->ParseFromArray((const void *)buffer, len)) {
    free(buffer);
    return true;
  } else {
    fprintf(stderr, "ReadProto: Failed parse proto.\n");
    free(buffer);
    return false;
  }
}

template <class T>
bool WriteProto(SOCKET sock, const T &t) {
  CHECK(sock != INVALID_SOCKET);
  string s = t.SerializeAsString();
  if (s.size() > MAX_MESSAGE) {
    fprintf(stderr, "Tried to send message too long.");
    abort();
  }
  uint32_t len = htonl((uint32_t)s.size());

  char header[4];
  memcpy(header, &len, 4);
  int ret = send(sock, header, 4, 0);
  if (4 != ret) {
    fprintf(stderr, "Failed to send length (got %d) err %d.\n",
            ret, WSAGetLastError());
    return false;
  }

  int sent = send(sock, s.c_str(), (int)s.size(), 0);
  if (sent != (int)s.size()) {
    return false;
  }

  return true;
}

template <class T>
bool SingleServer::WriteProto(const T &t) {
  CHECK(state_ == ACTIVE);
  bool r = ::WriteProto(peer_, t);
  if (!r) {
    fprintf(stderr, "SingleServer failed writeproto.\n");
    Hangup();
  }
  return r;
}

template <class T>
bool SingleServer::ReadProto(T *t) {
  CHECK(state_ == ACTIVE);
  bool r = ::ReadProto(peer_, t);
  if (!r) {
    fprintf(stderr, "SingleServer failed readproto.\n");
    Hangup();
  }
  return r;
}

#endif
