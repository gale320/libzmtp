/*  =========================================================================
    zmtp_channel - channel class

    Copyright (c) contributors as noted in the AUTHORS file.
    This file is part of libzmtp, the C ZMTP stack.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#include "zmtp_classes.h"
#include "zmtpnet.h"

//  ZMTP greeting (64 bytes)

struct zmtp_greeting {
    byte signature [10];
    byte version [2];
    byte mechanism [20];
    byte as_server [1];
    byte filler [31];
};

//  Structure of our class

struct _zmtp_channel_t {
    int fd;             //  BSD socket handle
};

static zmtp_endpoint_t *
    s_endpoint_from_str (const char *endpoint_str);
static int
    s_negotiate (zmtp_channel_t *self);

/*
static int
    s_tcp_send (int fd, const void *data, size_t len);
static int
    s_tcp_recv (int fd, void *buffer, size_t len);
*/

//  --------------------------------------------------------------------------
//  Constructor

zmtp_channel_t *
zmtp_channel_new ()
{
    zmtp_channel_t *self = (zmtp_channel_t *) zmalloc (sizeof *self);
    assert (self);              //  For now, memory exhaustion is fatal
    self->fd = -1;
    return self;
}


//  --------------------------------------------------------------------------
//  Destructor; closes fd if connected

void
zmtp_channel_destroy (zmtp_channel_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        zmtp_channel_t *self = *self_p;
        if (self->fd != -1)
            close (self->fd);
        free (self);
        *self_p = NULL;
    }
}


//  --------------------------------------------------------------------------
//  Connect channel to local endpoint

int
zmtp_channel_ipc_connect (zmtp_channel_t *self, const char *path)
{
    assert (self);

    if (self->fd != -1)
        return -1;

    zmtp_endpoint_t *endpoint =
        (zmtp_endpoint_t *) zmtp_ipc_endpoint_new (path);
    if (endpoint == NULL)
        return -1;

    self->fd = zmtp_endpoint_connect (endpoint);
    zmtp_endpoint_destroy (&endpoint);
    if (self->fd == -1)
        return -1;

    if (s_negotiate (self) == -1) {
        close (self->fd);
        self->fd = -1;
        return -1;
    }

    return 0;
}


//  --------------------------------------------------------------------------
//  Connect channel to TCP endpoint

int
zmtp_channel_tcp_connect (zmtp_channel_t *self,
                          const char *addr, unsigned short port)
{
    assert (self);

    if (self->fd != -1)
        return -1;

    zmtp_endpoint_t *endpoint =
        (zmtp_endpoint_t *) zmtp_tcp_endpoint_new (addr, port);
    if (endpoint == NULL)
        return -1;

    self->fd = zmtp_endpoint_connect (endpoint);
    zmtp_endpoint_destroy (&endpoint);
    if (self->fd == -1)
        return -1;

    if (s_negotiate (self) == -1) {
        close (self->fd);
        self->fd = -1;
        return -1;
    }

    return 0;
}


//  --------------------------------------------------------------------------
//  Connect channel

int
zmtp_channel_connect (zmtp_channel_t *self, const char *endpoint_str)
{
    assert (self);

    if (self->fd != -1)
        return -1;

    zmtp_endpoint_t *endpoint = s_endpoint_from_str (endpoint_str);
    if (endpoint == NULL)
        return -1;

    self->fd = zmtp_endpoint_connect (endpoint);
    zmtp_endpoint_destroy (&endpoint);
    if (self->fd == -1)
        return -1;

    if (s_negotiate (self) == -1) {
        close (self->fd);
        self->fd = -1;
        return -1;
    }

    return 0;
}


//  --------------------------------------------------------------------------
//  Connect channel

int
zmtp_channel_listen (zmtp_channel_t *self, const char *endpoint_str)
{
    assert (self);

    if (self->fd != -1)
        return -1;

    zmtp_endpoint_t *endpoint = s_endpoint_from_str (endpoint_str);
    if (endpoint == NULL)
        return -1;

    self->fd = zmtp_endpoint_listen (endpoint);
    zmtp_endpoint_destroy (&endpoint);
    if (self->fd == -1)
        return -1;

    if (s_negotiate (self) == -1) {
        close (self->fd);
        self->fd = -1;
        return -1;
    }

    return 0;
}

static zmtp_endpoint_t *
s_endpoint_from_str (const char *endpoint_str)
{
    if (strncmp (endpoint_str, "ipc://", 6) == 0)
        return (zmtp_endpoint_t *)
            zmtp_ipc_endpoint_new (endpoint_str + 6);
    else
    if (strncmp (endpoint_str, "tcp://", 6) == 0) {
        char *colon = strrchr (endpoint_str + 6, ':');
        if (colon == NULL)
            return NULL;
        else {
            const size_t addr_len = colon - endpoint_str - 6;
            char addr [addr_len + 1];
            memcpy (addr, endpoint_str + 6, addr_len);
            addr [addr_len] = '\0';
            const unsigned short port = atoi (colon + 1);
            return (zmtp_endpoint_t *)
                zmtp_tcp_endpoint_new (addr, port);
        }
    }
    else
        return NULL;
}


//  --------------------------------------------------------------------------
//  Negotiate a ZMTP channel
//  This currently does only ZMTP v3, and will reject older protocols.
//  TODO: test sending random/wrong data to this handler.

static int
s_negotiate (zmtp_channel_t *self)
{
    assert (self);
    assert (self->fd != -1);

    const int s = self->fd;

    //  This is our greeting (64 octets)
    const struct zmtp_greeting outgoing = {
        .signature = { 0xff, 0, 0, 0, 0, 0, 0, 0, 1, 0x7f },
        .version   = { 3, 0 },
        .mechanism = { 'N', 'U', 'L', 'L', '\0' }
    };
    //  Send protocol signature
    if (s_tcp_send (s, outgoing.signature, sizeof outgoing.signature) == -1)
        goto io_error;

    //  Read the first byte.
    struct zmtp_greeting incoming;
    if (s_tcp_recv (s, incoming.signature, 1) == -1)
        goto io_error;
    assert (incoming.signature [0] == 0xff);

    //  Read the rest of signature
    if (s_tcp_recv (s, incoming.signature + 1, 9) == -1)
        goto io_error;
    assert ((incoming.signature [9] & 1) == 1);

    //  Exchange major version numbers
    if (s_tcp_send (s, outgoing.version, 1) == -1)
        goto io_error;
    if (s_tcp_recv (s, incoming.version, 1) == -1)
        goto io_error;

    assert (incoming.version [0] == 3);

    //  Send the rest of greeting to the peer.
    if (s_tcp_send (s, outgoing.version + 1, 1) == -1)
        goto io_error;
    if (s_tcp_send (s, outgoing.mechanism, sizeof outgoing.mechanism) == -1)
        goto io_error;
    if (s_tcp_send (s, outgoing.as_server, sizeof outgoing.as_server) == -1)
        goto io_error;
    if (s_tcp_send (s, outgoing.filler, sizeof outgoing.filler) == -1)
        goto io_error;

    //  Receive the rest of greeting from the peer.
    if (s_tcp_recv (s, incoming.version + 1, 1) == -1)
        goto io_error;
    if (s_tcp_recv (s, incoming.mechanism, sizeof incoming.mechanism) == -1)
        goto io_error;
    if (s_tcp_recv (s, incoming.as_server, sizeof incoming.as_server) == -1)
        goto io_error;
    if (s_tcp_recv (s, incoming.filler, sizeof incoming.filler) == -1)
        goto io_error;

    //  Send READY command
    zmtp_msg_t *ready = zmtp_msg_from_const_data (0x04, "\5READY", 6);
    assert (ready);
    zmtp_channel_send (self, ready);
    zmtp_msg_destroy (&ready);

    //  Receive READY command
    ready = zmtp_channel_recv (self);
    if (!ready)
        goto io_error;
    assert ((zmtp_msg_flags (ready) & ZMTP_MSG_COMMAND) == ZMTP_MSG_COMMAND);
    zmtp_msg_destroy (&ready);

    return 0;

io_error:
    return -1;
}


//  --------------------------------------------------------------------------
//  Send a ZMTP message to the channel

int
zmtp_channel_send (zmtp_channel_t *self, zmtp_msg_t *msg)
{
    assert (self);
    assert (msg);

    byte frame_flags = 0;
    if ((zmtp_msg_flags (msg) & ZMTP_MSG_MORE) == ZMTP_MSG_MORE)
        frame_flags |= ZMTP_MORE_FLAG;
    if ((zmtp_msg_flags (msg) & ZMTP_MSG_COMMAND) == ZMTP_MSG_COMMAND)
        frame_flags |= ZMTP_COMMAND_FLAG;
    if (zmtp_msg_size (msg) > 255)
        frame_flags |= ZMTP_LARGE_FLAG;
    if (s_tcp_send (self->fd, &frame_flags, sizeof frame_flags) == -1)
        return -1;

    if (zmtp_msg_size (msg) <= 255) {
        const byte msg_size = zmtp_msg_size (msg);
        if (s_tcp_send (self->fd, &msg_size, sizeof msg_size) == -1)
            return -1;
    }
    else {
        byte buffer [8];
        const uint64_t msg_size = (uint64_t) zmtp_msg_size (msg);
        buffer [0] = msg_size >> 56;
        buffer [1] = msg_size >> 48;
        buffer [2] = msg_size >> 40;
        buffer [3] = msg_size >> 32;
        buffer [4] = msg_size >> 24;
        buffer [5] = msg_size >> 16;
        buffer [6] = msg_size >> 8;
        buffer [7] = msg_size;
        if (s_tcp_send (self->fd, buffer, sizeof buffer) == -1)
            return -1;
    }
    if (s_tcp_send (self->fd, zmtp_msg_data (msg), zmtp_msg_size (msg)) == -1)
        return -1;
    return 0;
}


//  --------------------------------------------------------------------------
//  Receive a ZMTP message off the channel

zmtp_msg_t *
zmtp_channel_recv (zmtp_channel_t *self)
{
    assert (self);

    byte frame_flags;
    size_t size;

    if (s_tcp_recv (self->fd, &frame_flags, 1) == -1)
        return NULL;
    //  Check large flag
    if ((frame_flags & ZMTP_LARGE_FLAG) == 0) {
        byte buffer [1];
        if (s_tcp_recv (self->fd, buffer, 1) == -1)
            return NULL;
        size = (size_t) buffer [0];
    }
    else {
        byte buffer [8];
        if (s_tcp_recv (self->fd, buffer, sizeof buffer) == -1)
            return NULL;
        size = (uint64_t) buffer [0] << 56 |
               (uint64_t) buffer [1] << 48 |
               (uint64_t) buffer [2] << 40 |
               (uint64_t) buffer [3] << 32 |
               (uint64_t) buffer [4] << 24 |
               (uint64_t) buffer [5] << 16 |
               (uint64_t) buffer [6] << 8  |
               (uint64_t) buffer [7];
    }
    byte *data = zmalloc (size);
    assert (data);
    if (s_tcp_recv (self->fd, data, size) == -1) {
        free (data);
        return NULL;
    }
    byte msg_flags = 0;
    if ((frame_flags & ZMTP_MORE_FLAG) == ZMTP_MORE_FLAG)
        msg_flags |= ZMTP_MSG_MORE;
    if ((frame_flags & ZMTP_COMMAND_FLAG) == ZMTP_COMMAND_FLAG)
        msg_flags |= ZMTP_MSG_COMMAND;
    return zmtp_msg_from_data (msg_flags, &data, size);
}

