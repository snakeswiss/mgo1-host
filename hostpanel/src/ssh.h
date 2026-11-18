// SSH to the server (libssh2 + OpenSSL): connect, sign in, run commands, send files.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

struct SshConfig {
    std::string host;                 // DNS name or IP
    int port = 22;
    std::string user = "root";
    bool useKey = false;
    std::string password;
    std::string keyPath, keyPass;
    std::string trustedFp;            // "SHA256:..." accepted before; empty: not known yet
};

enum class SshErr { None, Resolve, Connect, Handshake, HostKeyUnknown, HostKeyChanged, Auth, Channel, Timeout, Io, Cancelled };

class Ssh {
public:
    Ssh();
    ~Ssh();
    Ssh(const Ssh&) = delete;
    Ssh& operator=(const Ssh&) = delete;

    // connects and signs in; on HostKeyUnknown / HostKeyChanged, fingerprint() holds the server's key
    bool Open(const SshConfig& cfg, int timeoutMs = 10000);
    void Close();
    bool IsOpen() const { return session_ != nullptr && authed_; }
    SshErr err() const { return err_; }
    const std::string& errText() const { return errText_; }
    const std::string& fingerprint() const { return fp_; }

    // runs a command; its stdin gets `input`; returns the exit status (-1: not run; -2: started,
    // its end not seen: the connection was lost or it did not answer in time)
    int Exec(const std::string& cmd, const std::string& input, std::string& out, std::string* errOut = nullptr, int timeoutMs = 60000);
    // runs a command and hands each line of its output to onLine until it ends, onLine returns false or stop is set
    bool Stream(const std::string& cmd, const std::function<bool(const std::string&)>& onLine, const std::atomic<bool>& stop, int idleTimeoutMs = 15000);
    // sends a file (SCP); progress(done, total)
    bool Upload(const std::string& localPath, const std::string& remotePath, int mode,
                const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>& cancel);

    static void GlobalInit();

private:
    void Fail(SshErr e, const std::string& text);
    bool WaitSocket(int timeoutMs);
    uintptr_t sock_ = (uintptr_t)~0ull;
    struct _LIBSSH2_SESSION* session_ = nullptr;
    bool authed_ = false;
    SshErr err_ = SshErr::None;
    std::string errText_, fp_;
};

std::string Base64(const unsigned char* p, size_t n, bool pad = true);
