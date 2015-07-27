XDP: Xenia Debug Protocol
--------------------------

## Overview

### General

* Packets flow in both directions (client and server).
* Each packet is length-prefixed (exclusive) and tagged with a type.
* The entire protocol is fully asynchronous, and supports both request/response
  style RPCs as well as notifications.
* Request/response RPCs are matched on ID and multiple are allowed to be in
  flight at a time.
* Notifications may come in any order.

### Server

The debug server is single threaded and processes commands as a FIFO. This
means that all requests will be responded to in the order they are received.

### Client

TODO

## Packet Structure

Each packet consists of a length, type, and request ID. The length denotes
the total number of bytes within the packet body, excluding the packet header.
The request ID is an opaque value sent by the client when making a request and
the server returns whatever the ID is when it responds to that request. The ID
is unused for notifications.

```
struct Packet {
  uint16_t packet_type;
  uint16_t request_id;
  uint32_t body_length;
  uint8_t body[body_length];
};
```

## Packets

TODO(benvanik): document the various types, etc.
