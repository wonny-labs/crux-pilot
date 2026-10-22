#include "client.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>

namespace {
struct State {
    int closes = 0, sends = 0, recvs = 0, polls = 0;
    int socket_error = 0, fcntl_error = 0, connect_error = EINPROGRESS, so_error = 0;
    int close_error = 0, send_error = 0, recv_error = 0, clock_error = 0;
    int poll_interrupts = 0, io_interrupts = 0;
    bool silence = false, eof = false, corrupt = false, oversized = false;
    bool clock_failure_without_errno = false;
    short events = 0;
    size_t chunk = 7, sent = 0, received = 0;
    long ns = 0, poll_delay_ns = 0;
    unsigned char data[32]{};
} s;
class Client : public ::testing::Test {
    void SetUp() override {
        s = State{};
    }
};
} // namespace
extern "C" int networking_test_socket(int, int, int) {
    if (s.socket_error) {
        errno = s.socket_error;
        return -1;
    }
    return 42;
}
extern "C" int networking_test_fcntl(int, int, ...) {
    if (s.fcntl_error) {
        errno = s.fcntl_error;
        return -1;
    }
    return 0;
}
extern "C" int networking_test_connect(int, const sockaddr *, socklen_t) {
    if (s.connect_error) {
        errno = s.connect_error;
        return -1;
    }
    return 0;
}
extern "C" int networking_test_getsockopt(int, int, int, void *error, socklen_t *) {
    *static_cast<int *>(error) = s.so_error;
    return 0;
}
extern "C" int networking_test_poll(pollfd *fds, nfds_t, int remaining) {
    EXPECT_GT(remaining, 0);
    ++s.polls;
    s.ns += s.poll_delay_ns;
    if (s.poll_interrupts-- > 0) {
        errno = EINTR;
        return -1;
    }
    fds->revents = s.events ? s.events : fds->events;
    return s.silence ? 0 : 1;
}
extern "C" ssize_t networking_test_send(int, const void *data, size_t length, int flags) {
    EXPECT_EQ(flags, 0);
    ++s.sends;
    if (s.send_error || s.io_interrupts-- > 0) {
        errno = s.send_error ? s.send_error : EAGAIN;
        return -1;
    }
    size_t count = std::min(length, s.chunk);
    std::memcpy(s.data + s.sent, data, count);
    s.sent += count;
    return static_cast<ssize_t>(count);
}
extern "C" ssize_t networking_test_recv(int, void *data, size_t length, int flags) {
    EXPECT_EQ(flags, 0);
    ++s.recvs;
    if (s.recv_error) {
        errno = s.recv_error;
        return -1;
    }
    if (s.eof)
        return 0;
    if (s.oversized)
        return static_cast<ssize_t>(length);
    size_t count = std::min({length, s.chunk, sizeof(s.data) - s.received});
    std::memcpy(data, s.data + s.received, count);
    s.received += count;
    if (s.corrupt && count)
        static_cast<unsigned char *>(data)[0] ^= 1;
    return static_cast<ssize_t>(count);
}
extern "C" int networking_test_close(int) {
    ++s.closes;
    if (s.close_error) {
        errno = s.close_error;
        return -1;
    }
    return 0;
}
extern "C" int networking_test_clock_gettime(clockid_t, timespec *now) {
    if (s.clock_failure_without_errno) {
        errno = 0;
        return -1;
    }
    if (s.clock_error) {
        errno = s.clock_error;
        return -1;
    }
    s.ns += 1000000;
    now->tv_sec = 0;
    now->tv_nsec = s.ns;
    return 0;
}
TEST_F(Client, TcpHandlesShortTransfersWithinOneDeadline) {
    EXPECT_EQ(networking_exchange("127.0.0.1", 42420, false, 100), 0);
    EXPECT_EQ(s.sent, 32u);
    EXPECT_EQ(s.received, 32u);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, RetriesInterruptedPollAndWouldBlock) {
    s.poll_interrupts = 2;
    s.io_interrupts = 2;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), 0);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, RepeatedInterruptionsCannotExtendDeadline) {
    s.poll_interrupts = 1000;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 5), -ETIMEDOUT);
    EXPECT_LE(s.polls, 5);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, SilenceExpires) {
    s.silence = true;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -ETIMEDOUT);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, ConnectPollReadinessStillRequiresSocketError) {
    s.so_error = ECONNREFUSED;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -ECONNREFUSED);
    EXPECT_EQ(s.sends, 0);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, HangupIsReadAsEof) {
    s.events = POLLHUP;
    s.eof = true;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -ECONNRESET);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, FailedCloseIsNeverRetried) {
    s.close_error = EIO;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EIO);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, FailedCreationOwnsNoDescriptor) {
    s.socket_error = EMFILE;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EMFILE);
    EXPECT_EQ(s.closes, 0);
}
TEST_F(Client, SetupFailureClosesDescriptor) {
    s.fcntl_error = EIO;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EIO);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, SendFailureClosesDescriptor) {
    s.send_error = EPIPE;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EPIPE);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, RejectsInvalidInputBeforeSocketCreation) {
    EXPECT_EQ(networking_exchange(nullptr, 1, false, 100), -EINVAL);
    EXPECT_EQ(networking_exchange("bad", 1, false, 100), -EINVAL);
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 0), -EINVAL);
    EXPECT_EQ(s.closes, 0);
}
TEST_F(Client, UdpIsOneAtomicDatagramAndGuardByteDetectsOversize) {
    s.chunk = 32;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, true, 100), 0);
    EXPECT_EQ(s.sends, 1);
    EXPECT_EQ(s.recvs, 1);
    s = State{};
    s.chunk = 32;
    s.oversized = true;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, true, 100), -EMSGSIZE);
}
TEST_F(Client, UdpShortSendAndZeroDatagramAreNotTcpSemantics) {
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, true, 100), -EIO);
    s = State{};
    s.chunk = 32;
    s.eof = true;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, true, 100), -EMSGSIZE);
}
TEST_F(Client, RejectsCorruptEcho) {
    s.corrupt = true;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EBADMSG);
}

TEST_F(Client, ClockFailureOwnsNoSocket) {
    s.clock_error = EIO;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EIO);
    EXPECT_EQ(s.closes, 0);
}
TEST_F(Client, InvalidPollDescriptorAndReceiveErrorsCloseOnce) {
    s.events = POLLNVAL;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EBADF);
    EXPECT_EQ(s.closes, 1);
    s = State{};
    s.recv_error = ECONNRESET;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -ECONNRESET);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, WouldBlockCannotRenewOverallDeadline) {
    s.io_interrupts = 1000;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 8), -ETIMEDOUT);
    EXPECT_LE(s.sends, 8);
    EXPECT_EQ(s.closes, 1);
}

TEST_F(Client, ReadinessAfterExpiryDoesNotPermitAnotherTransfer) {
    s.connect_error = 0;
    s.poll_delay_ns = 200000000;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -ETIMEDOUT);
    EXPECT_EQ(s.sends, 0);
    EXPECT_EQ(s.closes, 1);
}
extern "C" {
#include "runtime_port.h"
}
TEST_F(Client, RuntimeRetriesInterruptedPoll) {
    s.poll_interrupts = 2;
    EXPECT_EQ(runtime_port_exchange("127.0.0.1", 1), 0);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, RuntimeInterruptionsCannotRenewDeadline) {
    s.poll_interrupts = 1000;
    EXPECT_EQ(runtime_port_exchange("127.0.0.1", 1), -ETIMEDOUT);
    EXPECT_LE(s.polls, 200);
    EXPECT_EQ(s.closes, 1);
}
TEST_F(Client, RuntimeReadinessAfterExpiryDoesNotPermitSend) {
    s.connect_error = 0;
    s.poll_delay_ns = 250000000;
    EXPECT_EQ(runtime_port_exchange("127.0.0.1", 1), -ETIMEDOUT);
    EXPECT_EQ(s.sends, 0);
    EXPECT_EQ(s.closes, 1);
}

TEST_F(Client, FailedClockCannotLookLikeSuccessEvenWithoutErrno) {
    s.clock_failure_without_errno = true;
    s.socket_error = EMFILE;
    EXPECT_EQ(networking_exchange("127.0.0.1", 1, false, 100), -EIO);
    EXPECT_EQ(s.closes, 0);
}
