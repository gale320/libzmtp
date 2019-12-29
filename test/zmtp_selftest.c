/*  =========================================================================
    zmtp_selftest - run self tests

    Copyright (c) contributors as noted in the AUTHORS file.
    This file is part of libzmtp, the C ZMTP stack.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#include "zmtpinc.h"


#include <poll.h>

//  Simple TCP echo server. It listens on a TCP port and after
//  accepting a new connection, echoes all received data.
//  This is to test the encodining/decoding compatibility.

struct echo_serv_t {
    unsigned short port;
};

static void *
s_echo_serv (void *arg)
{
    struct echo_serv_t *params = (struct echo_serv_t *) arg;

    //  Create socket
    const int s = socket (AF_INET, SOCK_STREAM, 0);
    assert (s != -1);
    
    //  Allow port reuse
    const int on = 1;
    int rc = setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    assert (rc == 0);
    
    //  Fill address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons (params->port);
    server_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    
    //  Bind socket
    rc = bind (s, (struct sockaddr *) &server_addr, sizeof server_addr);
    assert (rc == 0);
    
    //  Listen for connections
    rc = listen (s, 1);
    assert (rc != -1);
    
    //  Accept connection
    int fd = accept (s, NULL, NULL);
    assert (fd != -1);
    
    //  Set non-blocking mode
    const int flags = fcntl (fd, F_GETFL, 0);
    assert (flags != -1);
    rc = fcntl (fd, F_SETFL, flags | O_NONBLOCK);
    assert (rc == 0);
    unsigned char buf [80];
    
    //  Echo all received data
    while (1) {
        struct pollfd pollfd;
        pollfd.fd = fd;
        pollfd.events = POLLIN;
        rc = poll (&pollfd, 1, -1);
        assert (rc == 1);
        rc = read (fd, buf, sizeof buf);
        if (rc == 0)
            break;
        assert (rc > 0 || errno == EINTR);
        if (rc > 0) {
            rc = zmtp_tcp_send (fd, buf, rc);
            assert (rc == 0);
        }
    }
    close (fd);
    close (s);
    return NULL;
}

struct script_line {
    char cmd;           // 'i' for input, 'o' for output, 'x' terminator
    size_t data_len;    //  length of data
    const char *data;   //  data to send or expect
};

struct test_server_t {
    unsigned short port;
    const struct script_line *script;
};

static void *
s_test_server (void *arg)
{
    struct test_server_t *params = (struct test_server_t *) arg;

    //  Create socket
    const int s = socket (AF_INET, SOCK_STREAM, 0);
    assert (s != -1);
    //  Allow port reuse
    const int on = 1;
    int rc = setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    assert (rc == 0);
    //  Fill address
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons (params->port),
        .sin_addr.s_addr = htonl (INADDR_ANY),
    };
    //  Bind socket
    rc = bind (s, (struct sockaddr *) &server_addr, sizeof server_addr);
    assert (rc == 0);
    //  Listen for connections
    rc = listen (s, 1);
    assert (rc != -1);
    //  Accept connection
    const int fd = accept (s, NULL, NULL);
    assert (fd != -1);

    //  Run I/O script
    for (int i = 0; params->script [i].cmd != 'x'; i++) {
        const char cmd = params->script [i].cmd;
        const size_t data_len = params->script [i].data_len;
        const char *data = params->script [i].data;
        assert (cmd == 'i' || cmd == 'o');
        if (cmd == 'i') {
            char buf [data_len];
            const int rc = zmtp_tcp_recv (fd, buf, data_len);
            assert (rc == 0);
            assert (memcmp (buf, data, data_len) == 0);
        }
        else {
            const int rc = zmtp_tcp_send (fd, data, data_len);
            assert (rc == 0);
        }
    }

    close (fd);
    close (s);
    return NULL;
}

//  --------------------------------------------------------------------------
//  Selftest

void
zmtp_channel_test (bool verbose)
{
    printf (" * zmtp_channel: ");
    //  @selftest
    pthread_t thread;

    struct echo_serv_t echo_serv_params = { .port = 22001 };
    pthread_create (&thread, NULL, s_echo_serv, &echo_serv_params);
    sleep (1);
    zmtp_channel_t *channel = zmtp_channel_new ();
    assert (channel);
    int rc = zmtp_channel_tcp_connect (channel, "127.0.0.1", 22001);
    assert (rc == 0);
    char *test_strings [] = {
        "1",
        "22",
        "333",
        "4444",
        "55555"
    };
        
    for (int i = 0; i < 5; i++) {
        zmtp_msg_t *msg = zmtp_msg_from_const_data (
            0, test_strings [i], strlen (test_strings [i]));
        assert (msg);
        rc = zmtp_channel_send (channel, msg);
        assert (rc == 0);
        zmtp_msg_t *msg2 = zmtp_channel_recv (channel);
        assert (msg2 != NULL);
        assert (zmtp_msg_size (msg) == zmtp_msg_size (msg2));
        assert (memcmp (zmtp_msg_data (msg),
            zmtp_msg_data (msg2), zmtp_msg_size (msg)) == 0);
        zmtp_msg_destroy (&msg);
        zmtp_msg_destroy (&msg2);
    }
    zmtp_channel_destroy (&channel);
    pthread_join (thread, NULL);

    //  Test flow, initial handshake, receive "ping 1" and "ping 2" messages,
    //  then send "pong 1" and "ping 2"
    struct script_line script[] = {
        { 'o', 10, "\xFF\0\0\0\0\0\0\0\1\x7F" },
        { 'o', 2, "\3\0" },
        { 'o', 20, "NULL\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" },
        { 'o', 32, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" },
        { 'i', 10, "\xFF\0\0\0\0\0\0\0\1\x7F" },
        { 'i', 2, "\3\0" },
        { 'i', 20, "NULL\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" },
        { 'i', 32, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" },
        { 'o', 8, "\4\6\5READY" },     //  send READY command
        { 'i', 8, "\4\6\5READY" },     //  expect READY command
        { 'i', 8, "\1\6ping 1" },      //  expect ping 1, more set
        { 'i', 8, "\0\6ping 2" },      //  expect ping 2, more flag not set
        { 'o', 8, "\1\6pong 1" },      //  send pong 1, more set
        { 'o', 8, "\0\6pong 2" },      //  send pong 2, more flag not set
        { 'x' },
    };

    struct test_server_t params = {
        .port = 22000,
        .script = script,
    };
    pthread_create (&thread, NULL, s_test_server, &params);
    sleep (1);

    channel = zmtp_channel_new ();
    assert (channel);
    rc = zmtp_channel_tcp_connect (channel, "127.0.0.1", 22000);
    assert (rc == 0);

    //  Send "ping 1"
    zmtp_msg_t *ping_1 =
        zmtp_msg_from_const_data (ZMTP_MSG_MORE, "ping 1", 6);
    rc = zmtp_channel_send (channel, ping_1);
    assert (rc == 0);
    zmtp_msg_destroy (&ping_1);

    //  Send "ping 2"
    zmtp_msg_t *ping_2 =
        zmtp_msg_from_const_data (0, "ping 2", 6);
    rc = zmtp_channel_send (channel, ping_2);
    assert (rc == 0);
    zmtp_msg_destroy (&ping_2);

    //  Receive "pong 1"
    zmtp_msg_t *pong_1 = zmtp_channel_recv (channel);
    assert (pong_1 != NULL);
    assert (zmtp_msg_size (pong_1) == 6);
    assert (memcmp (zmtp_msg_data (pong_1), "pong 1", 6) == 0);
    assert ((zmtp_msg_flags (pong_1) & ZMTP_MSG_MORE) == ZMTP_MSG_MORE);
    zmtp_msg_destroy (&pong_1);

    //  Receive "pong 2"
    zmtp_msg_t *pong_2 = zmtp_channel_recv (channel);
    assert (pong_2 != NULL);
    assert (zmtp_msg_size (pong_2) == 6);
    assert (memcmp (zmtp_msg_data (pong_2), "pong 2", 6) == 0);
    assert ((zmtp_msg_flags (pong_2) & ZMTP_MSG_MORE) == 0);
    zmtp_msg_destroy (&pong_2);

    zmtp_channel_destroy (&channel);
    pthread_join (thread, NULL);

    //  @end
    printf ("OK\n");
}




void
zmtp_msg_test (bool verbose)
{
    printf (" * zmtp_msg: ");
    //  @selftest
    zmtp_msg_t *msg = zmtp_msg_from_const_data (0, "hello", 6);
    assert (msg);
    assert (zmtp_msg_flags (msg) == 0);
    assert (zmtp_msg_size (msg) == 6);
    assert (memcmp (zmtp_msg_data (msg), "hello", 6) == 0);
    zmtp_msg_destroy (&msg);
    assert (msg == NULL);
    //  @end
    printf ("OK\n");
}

int main (int argc, char *argv [])
{
//     bool verbose;
//     if (argc == 2 && strcmp (argv [1], "-v") == 0)
//         verbose = true;
//     else
//         verbose = false;
//
//     printf ("Running self tests...\n");
//     zmtp_msg_test (verbose);
//     printf ("Tests passed OK\n");
    zmtp_msg_test (false);
    zmtp_channel_test (false);
    return 0;
}
