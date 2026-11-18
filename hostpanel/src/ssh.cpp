#include "ssh.h"
#include "platform.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
#endif
#include <libssh2.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

void Ssh::GlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef _WIN32
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
#else
        signal(SIGPIPE, SIG_IGN);                     // a write on a closed link: an error, not the end of the program
#endif
        libssh2_init(0);
    });
}

static void SetBlocking(SOCKET s, bool on) {
#ifdef _WIN32
    u_long nb = on ? 0 : 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? fl & ~O_NONBLOCK : fl | O_NONBLOCK);
#endif
}

std::string Base64(const unsigned char* p, size_t n, bool pad) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63];
        if (i + 1 < n) o += t[v >> 6 & 63]; else if (pad) o += '=';
        if (i + 2 < n) o += t[v & 63]; else if (pad) o += '=';
    }
    return o;
}

Ssh::Ssh() { GlobalInit(); }
Ssh::~Ssh() { Close(); }

// text: "@key|arg" (TrMsg shows it in the program's language)
void Ssh::Fail(SshErr e, const std::string& text) {
    err_ = e;
    errText_ = text;
}

void Ssh::Close() {
    if (session_) {
        libssh2_session_disconnect(session_, "bye");
        libssh2_session_free(session_);
        session_ = nullptr;
    }
    if (sock_ != (uintptr_t)~0ull) { closesocket((SOCKET)sock_); sock_ = (uintptr_t)~0ull; }
    authed_ = false;
}

static SOCKET ConnectTcp(const std::string& host, int port, int timeoutMs, SshErr& err, std::string& text) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) { err = SshErr::Resolve; text = "@se_resolve|" + host; return INVALID_SOCKET; }
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
#ifdef __APPLE__
        { int one = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
        SetBlocking(s, false);
        connect(s, a->ai_addr, (int)a->ai_addrlen);
        fd_set w, e;
        FD_ZERO(&w); FD_SET(s, &w); FD_ZERO(&e); FD_SET(s, &e);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        int ok = 0;
        socklen_t len = sizeof ok;
        if (select((int)s + 1, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w) && getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&ok, &len) == 0 && ok == 0) {
            SetBlocking(s, true);
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
            break;
        }
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) { err = SshErr::Connect; text = "@se_reach|" + host + ":" + ps; }
    return s;
}

// keyboard-interactive: every prompt answered with the password
static thread_local const char* tKbdPassword = nullptr;
static void KbdAnswer(const char*, int, const char*, int, int n, const LIBSSH2_USERAUTH_KBDINT_PROMPT*, LIBSSH2_USERAUTH_KBDINT_RESPONSE* r, void**) {
    for (int i = 0; i < n; ++i) {
        const size_t len = tKbdPassword ? strlen(tKbdPassword) : 0;
        r[i].text = (char*)malloc(len + 1);
        if (r[i].text) memcpy(r[i].text, tKbdPassword ? tKbdPassword : "", len + 1);
        r[i].length = (unsigned)len;
    }
}

bool Ssh::Open(const SshConfig& cfg, int timeoutMs) {
    Close();
    err_ = SshErr::None; errText_.clear(); fp_.clear();
    SshErr e = SshErr::None;
    std::string t;
    SOCKET s = ConnectTcp(cfg.host, cfg.port, timeoutMs, e, t);
    if (s == INVALID_SOCKET) { Fail(e, t); return false; }
    sock_ = (uintptr_t)s;
    session_ = libssh2_session_init();
    if (!session_) { Fail(SshErr::Handshake, "@se_session"); Close(); return false; }
    libssh2_session_set_blocking(session_, 1);
    libssh2_session_set_timeout(session_, timeoutMs);
    if (libssh2_session_handshake(session_, s)) { Fail(SshErr::Handshake, "@se_handshake"); Close(); return false; }
    const char* h = libssh2_hostkey_hash(session_, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!h) { Fail(SshErr::Handshake, "@se_nohostkey"); Close(); return false; }
    std::string b = Base64((const unsigned char*)h, 32, false);
    fp_ = "SHA256:" + b;
    if (cfg.trustedFp.empty()) { Fail(SshErr::HostKeyUnknown, "@se_untrusted"); Close(); return false; }
    if (cfg.trustedFp != fp_) { Fail(SshErr::HostKeyChanged, "@se_keychanged"); Close(); return false; }
    int rc;
    if (cfg.useKey) {
        const std::string key = plat::ExpandHome(cfg.keyPath);
        rc = libssh2_userauth_publickey_fromfile(session_, cfg.user.c_str(), nullptr, key.c_str(), cfg.keyPass.empty() ? nullptr : cfg.keyPass.c_str());
    } else {
        rc = libssh2_userauth_password(session_, cfg.user.c_str(), cfg.password.c_str());
        if (rc) {
            const char* methods = libssh2_userauth_list(session_, cfg.user.c_str(), (unsigned)cfg.user.size());
            if (methods && strstr(methods, "keyboard-interactive")) {
                tKbdPassword = cfg.password.c_str();
                rc = libssh2_userauth_keyboard_interactive(session_, cfg.user.c_str(), &KbdAnswer);
                tKbdPassword = nullptr;
            }
        }
    }
    if (rc) { Fail(SshErr::Auth, cfg.useKey ? "@se_key_refused" : "@se_pw_refused"); Close(); return false; }
    // no keepalives here: with one set, libssh2's blocking calls give up ("Timed out waiting on
    // socket") as soon as the next keepalive is due, a command quiet for a few seconds included;
    // Stream sends its own
    libssh2_keepalive_config(session_, 1, 0);
    authed_ = true;
    return true;
}

bool Ssh::WaitSocket(int timeoutMs) {
    fd_set r, w;
    FD_ZERO(&r); FD_ZERO(&w);
    const int dir = libssh2_session_block_directions(session_);
    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) FD_SET((SOCKET)sock_, &r);
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) FD_SET((SOCKET)sock_, &w);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return select((int)(SOCKET)sock_ + 1, &r, &w, nullptr, &tv) > 0;
}

int Ssh::Exec(const std::string& cmd, const std::string& input, std::string& out, std::string* errOut, int timeoutMs) {
    out.clear();
    if (errOut) errOut->clear();
    if (!IsOpen()) { Fail(SshErr::Channel, "@se_notconn"); return -1; }
    libssh2_session_set_timeout(session_, timeoutMs);
    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(session_);
    if (!ch) { Fail(SshErr::Channel, "@se_channel"); Close(); return -1; }
    if (libssh2_channel_exec(ch, cmd.c_str())) { Fail(SshErr::Channel, "@se_exec"); libssh2_channel_free(ch); Close(); return -1; }
    size_t off = 0;
    while (off < input.size()) {
        ssize_t n = libssh2_channel_write(ch, input.data() + off, input.size() - off);
        if (n < 0) break;
        off += (size_t)n;
    }
    libssh2_channel_send_eof(ch);
    // from here the command runs: a failure is -2 (its end not seen), not to be run again blindly
    char buf[16384];
    for (;;) {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof buf);
        if (n > 0) { out.append(buf, (size_t)n); continue; }
        if (n == LIBSSH2_ERROR_TIMEOUT) { Fail(SshErr::Timeout, "@se_timeout"); libssh2_channel_free(ch); Close(); return -2; }
        if (n < 0) { Fail(SshErr::Io, "@se_read"); libssh2_channel_free(ch); Close(); return -2; }
        break;
    }
    for (;;) {
        ssize_t n = libssh2_channel_read_stderr(ch, buf, sizeof buf);
        if (n > 0) { if (errOut) errOut->append(buf, (size_t)n); continue; }
        break;
    }
    libssh2_channel_close(ch);
    libssh2_channel_wait_closed(ch);
    const int status = libssh2_channel_get_exit_status(ch);
    libssh2_channel_free(ch);
    return status;
}

bool Ssh::Stream(const std::string& cmd, const std::function<bool(const std::string&)>& onLine, const std::atomic<bool>& stop, int idleTimeoutMs) {
    if (!IsOpen()) { Fail(SshErr::Channel, "@se_notconn"); return false; }
    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(session_);
    if (!ch) { Fail(SshErr::Channel, "@se_channel"); Close(); return false; }
    if (libssh2_channel_exec(ch, cmd.c_str())) { Fail(SshErr::Channel, "@se_exec"); libssh2_channel_free(ch); Close(); return false; }
    libssh2_channel_send_eof(ch);
    libssh2_session_set_blocking(session_, 0);
    libssh2_keepalive_config(session_, 1, 15);      // a quiet command keeps the link (and NAT) alive; sent below
    std::string pending;
    char buf[16384];
    uint64_t last = plat::TickMs();
    bool ok = true;
    while (!stop) {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof buf);
        if (n > 0) {
            last = plat::TickMs();
            pending.append(buf, (size_t)n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (!onLine(line)) { ok = true; goto done; }
            }
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN) {
            int next = 0;
            libssh2_keepalive_send(session_, &next);
            if (plat::TickMs() - last > (uint64_t)idleTimeoutMs) { Fail(SshErr::Timeout, "@se_nodata"); ok = false; break; }
            WaitSocket(250);
            continue;
        }
        if (n == 0) {
            if (libssh2_channel_eof(ch)) { Fail(SshErr::Io, "@se_ended"); ok = false; break; }
            WaitSocket(250);
            continue;
        }
        Fail(SshErr::Io, "@se_lost");
        ok = false;
        break;
    }
done:
    libssh2_keepalive_config(session_, 1, 0);
    libssh2_session_set_blocking(session_, 1);
    if (ok || stop) {
        libssh2_channel_close(ch);
        libssh2_channel_free(ch);
        return true;
    }
    libssh2_channel_free(ch);
    Close();
    return false;
}

bool Ssh::Upload(const std::string& localPath, const std::string& remotePath, int mode,
                 const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>& cancel) {
    if (!IsOpen()) { Fail(SshErr::Channel, "@se_notconn"); return false; }
    FILE* f = plat::Open(localPath, "rb");
    if (!f) { Fail(SshErr::Io, "@se_localread"); return false; }
    plat::Seek(f, 0, SEEK_END);
    const uint64_t total = (uint64_t)plat::Tell(f);
    plat::Seek(f, 0, SEEK_SET);
    libssh2_session_set_timeout(session_, 60000);
    LIBSSH2_CHANNEL* ch = libssh2_scp_send64(session_, remotePath.c_str(), mode, (libssh2_int64_t)total, 0, 0);
    if (!ch) { fclose(f); Fail(SshErr::Channel, "@se_send|" + remotePath); Close(); return false; }
    std::vector<char> buf(1 << 20);
    uint64_t done = 0;
    bool ok = true;
    while (done < total) {
        if (cancel) { Fail(SshErr::Cancelled, "@se_cancelled"); ok = false; break; }
        const size_t n = fread(buf.data(), 1, buf.size(), f);
        if (!n) { Fail(SshErr::Io, "@se_localread"); ok = false; break; }
        size_t off = 0;
        while (off < n) {
            ssize_t w = libssh2_channel_write(ch, buf.data() + off, n - off);
            if (w < 0) { Fail(SshErr::Io, "@se_upload"); ok = false; break; }
            off += (size_t)w;
        }
        if (!ok) break;
        done += n;
        if (progress) progress(done, total);
    }
    fclose(f);
    if (!ok) { libssh2_channel_free(ch); Close(); return false; }
    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    return true;
}
