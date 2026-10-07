// Logs which socket operations the app sandbox allows (autoplay runs only):
// the WebRTC stack (libjuice) needs non-blocking UDP, pipes and poll.
#include "util/log.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace xc::platform {

void probeNetworking() {
    auto res = [](const char* what, int rc) { XC_LOGI("probe %-28s rc=%d errno=%d", what, rc, rc < 0 ? errno : 0); };

    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    res("socket(UDP)", s);
    if (s >= 0) {
        int one = 1;
        errno = 0;
        res("ioctl(FIONBIO)", ::ioctl(s, FIONBIO, &one));
        errno = 0;
        int fl = ::fcntl(s, F_GETFL, 0);
        res("fcntl(F_GETFL)", fl);
        errno = 0;
        res("fcntl(F_SETFL,O_NONBLOCK)", ::fcntl(s, F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK));
        errno = 0;
        res("setsockopt(SO_NBIO 0x1200)", ::setsockopt(s, SOL_SOCKET, 0x1200, &one, sizeof one));
        int buf = 1 << 20;
        errno = 0;
        res("setsockopt(SO_RCVBUF)", ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf));
        sockaddr_in a{};
        a.sin_len = sizeof a;
        a.sin_family = AF_INET;
        errno = 0;
        res("bind(0.0.0.0:0)", ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a));
        socklen_t len = sizeof a;
        errno = 0;
        res("getsockname", ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len));
        XC_LOGI("probe bound port %u", ntohs(a.sin_port));
        char tmp[1];
        errno = 0;
        res("recvfrom (nonblocking?)", static_cast<int>(::recvfrom(s, tmp, 1, MSG_DONTWAIT, nullptr, nullptr)));
        sockaddr_in dst{};
        dst.sin_len = sizeof dst;
        dst.sin_family = AF_INET;
        dst.sin_port = htons(3478);
        ::inet_pton(AF_INET, "20.0.0.1", &dst.sin_addr);
        errno = 0;
        res("sendto", static_cast<int>(::sendto(s, "x", 1, 0, reinterpret_cast<sockaddr*>(&dst), sizeof dst)));
        pollfd p{s, POLLIN, 0};
        errno = 0;
        res("poll(udp, 10ms)", ::poll(&p, 1, 10));
        ::close(s);
    }

    int fds[2] = {-1, -1};
    errno = 0;
    res("pipe", ::pipe(fds));
    if (fds[0] >= 0) {
        errno = 0;
        res("fcntl(pipe,O_NONBLOCK)", ::fcntl(fds[0], F_SETFL, O_NONBLOCK));
        errno = 0;
        res("write(pipe)", static_cast<int>(::write(fds[1], "x", 1)));
        pollfd p{fds[0], POLLIN, 0};
        errno = 0;
        res("poll(pipe)", ::poll(&p, 1, 10));
        ::close(fds[0]);
        ::close(fds[1]);
    }
    int sp[2] = {-1, -1};
    errno = 0;
    res("socketpair(AF_UNIX,DGRAM)", ::socketpair(AF_UNIX, SOCK_DGRAM, 0, sp));
    if (sp[0] >= 0) {
        ::close(sp[0]);
        ::close(sp[1]);
    }

    ifaddrs* ifa = nullptr;
    errno = 0;
    res("getifaddrs", ::getifaddrs(&ifa));
    for (ifaddrs* i = ifa; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        char ip[32];
        ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, ip, sizeof ip);
        XC_LOGI("probe interface %s %s", i->ifa_name, ip);
    }
    if (ifa) ::freeifaddrs(ifa);
}

}  // namespace xc::platform
