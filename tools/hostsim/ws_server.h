#pragma once
// Minimal localhost WebSocket (RFC 6455) + static-file HTTP server for armlive.
// Own implementation, POSIX sockets + poll(), no external dependencies:
//   - SHA-1 + base64 for the Sec-WebSocket-Accept handshake
//   - text frames both ways (client frames masked, server frames unmasked),
//     fragmented messages reassembled, ping -> pong, close -> close
//   - a plain HTTP/1.0-style GET server that serves one file (ui.html)
// Single-threaded: Run() polls every socket and calls the hooks from the
// calling thread. Binds 127.0.0.1 only.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace ws {

// ---- SHA-1 (FIPS 180-1) ----------------------------------------------------
inline void Sha1(const std::string& msg, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::string m = msg;
  const uint64_t bits = (uint64_t)msg.size() * 8;
  m.push_back((char)0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; i--) m.push_back((char)(bits >> (i * 8)));
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)(uint8_t)m[off + 4 * i] << 24) | ((uint32_t)(uint8_t)m[off + 4 * i + 1] << 16) |
             ((uint32_t)(uint8_t)m[off + 4 * i + 2] << 8) | (uint32_t)(uint8_t)m[off + 4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
      else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
      else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

inline std::string Base64(const uint8_t* p, size_t n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16;
    if (i + 1 < n) v |= (uint32_t)p[i + 1] << 8;
    if (i + 2 < n) v |= p[i + 2];
    o += T[(v >> 18) & 63];
    o += T[(v >> 12) & 63];
    o += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    o += (i + 2 < n) ? T[v & 63] : '=';
  }
  return o;
}

inline std::string AcceptKey(const std::string& key) {
  uint8_t d[20];
  Sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", d);
  return Base64(d, 20);
}

// Case-insensitive header lookup in a raw HTTP request head.
inline std::string Header(const std::string& head, const char* name) {
  const size_t nl = strlen(name);
  size_t pos = 0;
  while ((pos = head.find("\r\n", pos)) != std::string::npos) {
    pos += 2;
    if (head.size() - pos > nl && strncasecmp(head.c_str() + pos, name, nl) == 0 && head[pos + nl] == ':') {
      size_t v = pos + nl + 1, e = head.find("\r\n", v);
      while (v < e && (head[v] == ' ' || head[v] == '\t')) v++;
      while (e > v && (head[e - 1] == ' ' || head[e - 1] == '\t')) e--;
      return head.substr(v, e - v);
    }
  }
  return "";
}

inline std::string Frame(int opcode, const std::string& payload) {
  std::string f;
  f += (char)(0x80 | opcode);
  const size_t n = payload.size();
  if (n < 126) f += (char)n;
  else if (n < 65536) { f += (char)126; f += (char)(n >> 8); f += (char)(n & 255); }
  else { f += (char)127; for (int i = 7; i >= 0; i--) f += (char)((uint64_t)n >> (8 * i)); }
  return f + payload;
}

struct Client {
  int fd = -1;
  bool http = false;        // plain HTTP file client (port 2)
  bool open = false;        // WebSocket handshake done
  bool closing = false;     // close after the write buffer drains
  std::string rbuf, wbuf, frag;
  int id = 0;
};

class Server {
 public:
  std::function<void(Client&)> on_open;                       // WebSocket opened
  std::function<void(Client&, const std::string&)> on_text;   // one text message
  std::function<std::string()> http_body;                     // body for GET / (empty = 404)

  bool Listen(int ws_port, int http_port, std::string& err) {
    lws_ = ListenOn(ws_port, err);
    if (lws_ < 0) return false;
    lhttp_ = ListenOn(http_port, err);
    return lhttp_ >= 0;
  }

  void Send(Client& c, const std::string& text) {
    if (c.open && !c.closing) c.wbuf += Frame(1, text);
  }
  void Broadcast(const std::string& text) {
    for (auto& c : clients_) Send(c, text);
  }
  size_t OpenCount() const {
    size_t n = 0;
    for (auto& c : clients_) n += c.open ? 1 : 0;
    return n;
  }

  // One poll pass (timeout in ms).
  void Poll(int timeout_ms) {
    std::vector<pollfd> p;
    p.push_back({lws_, POLLIN, 0});
    p.push_back({lhttp_, POLLIN, 0});
    for (auto& c : clients_) p.push_back({c.fd, (short)(POLLIN | (c.wbuf.empty() ? 0 : POLLOUT)), 0});
    if (poll(p.data(), p.size(), timeout_ms) < 0) return;
    if (p[0].revents & POLLIN) Accept(lws_, false);
    if (p[1].revents & POLLIN) Accept(lhttp_, true);
    for (size_t i = 2; i < p.size(); i++) {
      Client& c = clients_[i - 2];
      if (p[i].revents & (POLLIN | POLLHUP | POLLERR)) Read(c);
      if (c.fd >= 0 && !c.wbuf.empty()) Write(c);
      if (c.fd >= 0 && c.closing && c.wbuf.empty()) { close(c.fd); c.fd = -1; }
    }
    std::vector<Client> keep;
    for (auto& c : clients_) if (c.fd >= 0) keep.push_back(std::move(c));
    clients_.swap(keep);
  }

  // Flush pending writes (call after Broadcast outside Poll).
  void Flush() { for (auto& c : clients_) if (c.fd >= 0 && !c.wbuf.empty()) Write(c); }

 private:
  int lws_ = -1, lhttp_ = -1, next_id_ = 1;
  std::vector<Client> clients_;

  static int ListenOn(int port, std::string& err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { err = "socket() failed"; return -1; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (sockaddr*)&a, sizeof a) < 0 || listen(fd, 8) < 0) {
      err = "cannot listen on 127.0.0.1:" + std::to_string(port) + " (" + strerror(errno) + ")";
      close(fd);
      return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
  }

  void Accept(int lfd, bool http) {
    int fd = accept(lfd, nullptr, nullptr);
    if (fd < 0) return;
    fcntl(fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    Client c;
    c.fd = fd;
    c.http = http;
    c.id = next_id_++;
    clients_.push_back(std::move(c));
  }

  void Write(Client& c) {
    const ssize_t n = send(c.fd, c.wbuf.data(), c.wbuf.size(), 0);
    if (n > 0) c.wbuf.erase(0, (size_t)n);
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { close(c.fd); c.fd = -1; }
  }

  void Read(Client& c) {
    char buf[8192];
    const ssize_t n = recv(c.fd, buf, sizeof buf, 0);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      close(c.fd); c.fd = -1; return;
    }
    c.rbuf.append(buf, (size_t)n);
    if (c.rbuf.size() > (1u << 20)) { close(c.fd); c.fd = -1; return; }   // 1 MB guard
    if (!c.open) {
      const size_t e = c.rbuf.find("\r\n\r\n");
      if (e == std::string::npos) return;
      const std::string head = c.rbuf.substr(0, e + 2);
      c.rbuf.erase(0, e + 4);
      if (c.http) { ServeHttp(c, head); return; }
      const std::string key = Header(head, "Sec-WebSocket-Key");
      if (key.empty() || head.compare(0, 4, "GET ") != 0) {
        c.wbuf = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        c.closing = true;
        return;
      }
      c.wbuf += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + AcceptKey(key) + "\r\n\r\n";
      c.open = true;
      if (on_open) on_open(c);
    }
    ParseFrames(c);
  }

  void ServeHttp(Client& c, const std::string& head) {
    std::string path;
    if (head.compare(0, 4, "GET ") == 0) {
      const size_t sp = head.find(' ', 4);
      path = head.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
      const size_t q = path.find('?');
      if (q != std::string::npos) path.resize(q);
    }
    const bool root = (path == "/" || path == "/ui.html");
    const std::string body = (root && http_body) ? http_body() : std::string();
    if (body.empty()) {
      const std::string msg = "not found\n";
      c.wbuf = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: " +
               std::to_string(msg.size()) + "\r\nConnection: close\r\n\r\n" + msg;
    } else {
      c.wbuf = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
               "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }
    c.closing = true;
  }

  void ParseFrames(Client& c) {
    std::string& b = c.rbuf;
    while (c.fd >= 0 && b.size() >= 2) {
      const uint8_t b0 = (uint8_t)b[0], b1 = (uint8_t)b[1];
      const bool fin = b0 & 0x80, masked = b1 & 0x80;
      const int op = b0 & 0x0f;
      uint64_t len = b1 & 0x7f;
      size_t hdr = 2;
      if (len == 126) {
        if (b.size() < 4) return;
        len = ((uint64_t)(uint8_t)b[2] << 8) | (uint8_t)b[3];
        hdr = 4;
      } else if (len == 127) {
        if (b.size() < 10) return;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | (uint8_t)b[2 + i];
        hdr = 10;
      }
      if (len > (1u << 20)) { close(c.fd); c.fd = -1; return; }
      const size_t mk = hdr;
      if (masked) hdr += 4;
      if (b.size() < hdr + len) return;
      std::string pl = b.substr(hdr, (size_t)len);
      if (masked) for (size_t i = 0; i < pl.size(); i++) pl[i] ^= b[mk + (i & 3)];
      b.erase(0, hdr + (size_t)len);
      switch (op) {
        case 0x0: c.frag += pl; if (fin) { Deliver(c, c.frag); c.frag.clear(); } break;
        case 0x1: if (fin) Deliver(c, pl); else c.frag = pl; break;
        case 0x8: c.wbuf += Frame(0x8, pl.substr(0, 2)); c.closing = true; c.open = false; return;
        case 0x9: c.wbuf += Frame(0xA, pl); break;
        default: break;   // binary / pong: ignored
      }
    }
  }

  void Deliver(Client& c, const std::string& msg) {
    if (!on_text) return;
    size_t s = 0;   // JSON lines: one object per line, several lines per frame allowed
    while (s < msg.size()) {
      size_t e = msg.find('\n', s);
      if (e == std::string::npos) e = msg.size();
      if (e > s) on_text(c, msg.substr(s, e - s));
      s = e + 1;
    }
  }
};

}  // namespace ws
