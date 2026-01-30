#pragma once

namespace ao::pack::network {
/*
Network format types

FIXED k => where k <= 64 
ALIGN => align to next byte boundary
FIXEDLEN k <repeated> => length prefixed array of items with max size of k bits
LEN <repeated> => length prefixed array of items LEN is varint encoded


Envelope format
<MESSAGE ID> <PAYLOAD>
*/

}