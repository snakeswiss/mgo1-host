// The secrets kept on Linux and macOS (platform.h Protect / Unprotect): AES-256-GCM (OpenSSL), the key
// 32 random bytes in secret.key in the settings folder, a file only the user can read (0600, made
// once). A value: Base64 of "MGS1", a 12-byte nonce, the ciphertext, the 16-byte tag; "MGS1" is
// authenticated too. A value that does not decrypt (another key, a Windows file) gives "".
#ifndef _WIN32
#include "platform.h"
#include "ssh.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

namespace plat {
namespace {

constexpr unsigned char kTag[4] = {'M', 'G', 'S', '1'};
constexpr size_t kNonce = 12, kMac = 16, kHead = sizeof kTag + kNonce;

// 1 read, 0 no file, -1 unreadable
int ReadKey(const std::string& path, unsigned char k[32]) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) == 0 && (st.st_mode & 077)) fchmod(fd, 0600);    // only the user
    const bool ok = read(fd, k, 32) == 32;
    close(fd);
    return ok ? 1 : -1;
}

// the key, read once (made the first time)
bool Key(unsigned char out[32]) {
    static std::mutex mu;
    static unsigned char k[32];
    static bool have = false;
    std::lock_guard<std::mutex> g(mu);
    if (!have) {
        const std::string path = Join(ConfigDir(), "secret.key");
        const int r = ReadKey(path, k);
        have = r == 1;
        if (r == 0) {
            unsigned char nk[32];
            if (RAND_bytes(nk, sizeof nk) == 1) {
                const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
                if (fd >= 0) {
                    have = write(fd, nk, sizeof nk) == (ssize_t)sizeof nk && fsync(fd) == 0;
                    close(fd);
                    if (have) memcpy(k, nk, sizeof nk);
                    else unlink(path.c_str());
                } else if (errno == EEXIST) {
                    have = ReadKey(path, k) == 1;               // made by another instance just now
                }
            }
            OPENSSL_cleanse(nk, sizeof nk);
        }
    }
    if (have) memcpy(out, k, 32);
    return have;
}

std::vector<unsigned char> FromBase64(const std::string& b64) {
    std::vector<unsigned char> raw;
    unsigned v = 0; int bits = 0;
    for (char c : b64) {
        const int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) continue;
        v = v << 6 | (unsigned)d; bits += 6;
        if (bits >= 8) { bits -= 8; raw.push_back((unsigned char)(v >> bits)); }
    }
    return raw;
}

}  // namespace

std::string Protect(const std::string& clear) {
    if (clear.empty()) return {};
    unsigned char key[32];
    if (!Key(key)) return {};
    std::vector<unsigned char> out(kHead + clear.size() + kMac);
    memcpy(out.data(), kTag, sizeof kTag);
    bool ok = RAND_bytes(out.data() + sizeof kTag, (int)kNonce) == 1;
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, fin = 0;
    ok = ok && c && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
         EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, (int)kNonce, nullptr) == 1 &&
         EVP_EncryptInit_ex(c, nullptr, nullptr, key, out.data() + sizeof kTag) == 1 &&
         EVP_EncryptUpdate(c, nullptr, &n, kTag, (int)sizeof kTag) == 1 &&
         EVP_EncryptUpdate(c, out.data() + kHead, &n, (const unsigned char*)clear.data(), (int)clear.size()) == 1 &&
         EVP_EncryptFinal_ex(c, out.data() + kHead + n, &fin) == 1 && (size_t)(n + fin) == clear.size() &&
         EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, (int)kMac, out.data() + kHead + clear.size()) == 1;
    EVP_CIPHER_CTX_free(c);
    OPENSSL_cleanse(key, sizeof key);
    return ok ? Base64(out.data(), out.size()) : std::string();
}

std::string Unprotect(const std::string& b64) {
    if (b64.empty()) return {};
    const std::vector<unsigned char> raw = FromBase64(b64);
    if (raw.size() < kHead + kMac || memcmp(raw.data(), kTag, sizeof kTag) != 0) return {};
    unsigned char key[32];
    if (!Key(key)) return {};
    const size_t len = raw.size() - kHead - kMac;
    std::vector<unsigned char> clear(len + 1);
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int n = 0, fin = 0;
    const bool ok = c && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
                    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, (int)kNonce, nullptr) == 1 &&
                    EVP_DecryptInit_ex(c, nullptr, nullptr, key, raw.data() + sizeof kTag) == 1 &&
                    EVP_DecryptUpdate(c, nullptr, &n, kTag, (int)sizeof kTag) == 1 &&
                    EVP_DecryptUpdate(c, clear.data(), &n, raw.data() + kHead, (int)len) == 1 &&
                    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, (int)kMac, (void*)(raw.data() + kHead + len)) == 1 &&
                    EVP_DecryptFinal_ex(c, clear.data() + n, &fin) == 1 && (size_t)(n + fin) == len;
    EVP_CIPHER_CTX_free(c);
    OPENSSL_cleanse(key, sizeof key);
    std::string s = ok ? std::string((const char*)clear.data(), len) : std::string();
    OPENSSL_cleanse(clear.data(), clear.size());
    return s;
}

}  // namespace plat
#endif
