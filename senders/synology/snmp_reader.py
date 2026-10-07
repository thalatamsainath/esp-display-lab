"""Small read-only SNMPv2c GET/GETNEXT client. Python standard library only.

No SET requests are implemented. Designed for querying localhost on the NAS.
"""
import random
import socket
import time


class SnmpError(Exception):
    pass


def tlv(tag, payload):
    size = len(payload)
    if size < 128:
        length = bytes([size])
    else:
        encoded = size.to_bytes((size.bit_length() + 7) // 8, "big")
        length = bytes([0x80 | len(encoded)]) + encoded
    return bytes([tag]) + length + payload


def integer(value):
    if value < 0:
        raise ValueError("Only nonnegative request integers are supported")
    raw = value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big")
    return tlv(2, (b"\x00" if raw[0] & 0x80 else b"") + raw)


def base128(value):
    result = [value & 127]
    value >>= 7
    while value:
        result.insert(0, 128 | (value & 127))
        value >>= 7
    return bytes(result)


def encode_oid(oid):
    parts = tuple(int(x) for x in oid.strip(".").split(".")) if isinstance(oid, str) else tuple(oid)
    if len(parts) < 2 or parts[0] not in (0, 1, 2) or parts[1] < 0 or (parts[0] < 2 and parts[1] > 39) or any(x < 0 for x in parts):
        raise ValueError("Invalid OID")
    return tlv(6, base128(parts[0] * 40 + parts[1]) + b"".join(base128(x) for x in parts[2:]))


def decode_oid(raw):
    values, current = [], 0
    for byte in raw:
        current = (current << 7) | (byte & 127)
        if not byte & 128:
            values.append(current)
            current = 0
    if not values or raw[-1] & 128:
        raise SnmpError("Invalid response OID")
    first = min(2, values[0] // 40)
    return (first, values[0] - 40 * first, *values[1:])


def read_tlv(raw, position=0):
    if position + 2 > len(raw):
        raise SnmpError("Truncated SNMP response")
    tag, size = raw[position:position + 2]
    position += 2
    if size & 128:
        count = size & 127
        if not count or count > 4 or position + count > len(raw):
            raise SnmpError("Invalid ASN.1 length")
        size = int.from_bytes(raw[position:position + count], "big")
        position += count
    end = position + size
    if end > len(raw):
        raise SnmpError("Truncated ASN.1 value")
    return tag, raw[position:end], end


def parts(raw):
    values, position = [], 0
    while position < len(raw):
        tag, value, position = read_tlv(raw, position)
        values.append((tag, value))
    return values


def decode_response(raw, request_id, community):
    tag, body, end = read_tlv(raw)
    if tag != 0x30 or end != len(raw):
        raise SnmpError("Invalid SNMP message")
    message = parts(body)
    if len(message) != 3 or message[0] != (2, b"\x01") or message[1] != (4, community) or message[2][0] != 0xA2:
        raise SnmpError("Unexpected SNMP response")
    pdu = parts(message[2][1])
    if len(pdu) != 4 or any(tag != 2 for tag, _ in pdu[:3]) or pdu[3][0] != 0x30:
        raise SnmpError("Invalid response PDU")
    if int.from_bytes(pdu[0][1], "big", signed=True) != request_id:
        raise SnmpError("Mismatched SNMP request ID")
    error = int.from_bytes(pdu[1][1], "big", signed=True)
    if error:
        raise SnmpError("SNMP error status %d" % error)
    result = []
    for tag, binding in parts(pdu[3][1]):
        if tag != 0x30:
            raise SnmpError("Invalid variable binding")
        pair = parts(binding)
        if len(pair) != 2 or pair[0][0] != 6:
            raise SnmpError("Invalid binding OID")
        kind, value = pair[1]
        if kind in (0x80, 0x81, 0x82, 5):
            decoded = None
        elif kind in (2, 0x41, 0x42, 0x43, 0x46):
            decoded = int.from_bytes(value, "big", signed=kind == 2)
        elif kind == 4:
            decoded = value.decode("utf-8", errors="replace")
        elif kind == 6:
            decoded = decode_oid(value)
        else:
            raise SnmpError("Unsupported SNMP value type %d" % kind)
        result.append((decode_oid(pair[0][1]), decoded))
    return result


class Client:
    def __init__(self, host, community, port=161, timeout=2):
        if not isinstance(community, str) or not 1 <= len(community.encode()) <= 64:
            raise ValueError("Set the SNMP community name in sender-config.json")
        self.host, self.port = host, int(port)
        self.community = community.encode()
        self.timeout = float(timeout)

    def request(self, oid, next_value=False):
        request_id = random.randint(1, 0x7FFFFFFF)
        binding = tlv(0x30, encode_oid(oid) + tlv(5, b""))
        pdu = tlv(0xA1 if next_value else 0xA0, integer(request_id) + integer(0) + integer(0) + tlv(0x30, binding))
        message = tlv(0x30, integer(1) + tlv(4, self.community) + pdu)
        # UDP connect restricts replies to the requested agent/port.
        family, kind, protocol, _, address = socket.getaddrinfo(self.host, self.port, type=socket.SOCK_DGRAM)[0]
        with socket.socket(family, kind, protocol) as sock:
            sock.connect(address)
            for _ in range(2):
                sock.send(message)
                deadline = time.monotonic() + self.timeout
                while time.monotonic() < deadline:
                    sock.settimeout(max(0.01, deadline - time.monotonic()))
                    try:
                        received = sock.recv(65535)
                    except socket.timeout:
                        break
                    response = decode_response(received, request_id, self.community)
                    if len(response) != 1:
                        raise SnmpError("Expected one variable binding")
                    return response[0]
        raise SnmpError("SNMP timed out; check the local SNMP service and community")

    def walk(self, oid):
        prefix = tuple(int(part) for part in oid.strip(".").split("."))
        current, result = prefix, {}
        for _ in range(4096):
            returned, value = self.request(current, next_value=True)
            if returned[:len(prefix)] != prefix or value is None:
                return result
            if returned <= current:
                raise SnmpError("SNMP walk did not advance")
            result[returned[len(prefix):]] = value
            current = returned
        raise SnmpError("SNMP table exceeded its row limit")
