const WIRE = { VARINT: 0, FIXED64: 1, LENGTH_DELIMITED: 2, FIXED32: 5 };

function varint_bytes(value) {
    // Negative int32/enum values use the 10-byte two's complement form protoc emits.
    let remaining = BigInt.asUintN(64, BigInt(value));
    const bytes = [];
    do {
        let byte = Number(remaining & 0x7fn);
        remaining >>= 7n;
        if (remaining)
            byte |= 0x80;
        bytes.push(byte);
    } while (remaining);
    return Buffer.from(bytes);
}

class Writer {
    constructor() {
        this.chunks = [];
    }

    tag(field, wire_type) {
        this.chunks.push(varint_bytes((field << 3) | wire_type));
    }

    varint(field, value) {
        if (value === undefined || value === null)
            return this;
        this.tag(field, WIRE.VARINT);
        this.chunks.push(varint_bytes(value));
        return this;
    }

    bool(field, value) {
        if (value === undefined || value === null)
            return this;
        return this.varint(field, value ? 1 : 0);
    }

    fixed64(field, value) {
        if (value === undefined || value === null)
            return this;
        const bytes = Buffer.alloc(8);
        bytes.writeBigUInt64LE(BigInt.asUintN(64, BigInt(value)));
        this.tag(field, WIRE.FIXED64);
        this.chunks.push(bytes);
        return this;
    }

    bytes(field, value) {
        if (value === undefined || value === null)
            return this;
        const buffer = Buffer.isBuffer(value) ? value : Buffer.from(value);
        this.tag(field, WIRE.LENGTH_DELIMITED);
        this.chunks.push(varint_bytes(buffer.length), buffer);
        return this;
    }

    string(field, value) {
        if (value === undefined || value === null)
            return this;
        return this.bytes(field, Buffer.from(String(value), 'utf8'));
    }

    message(field, writer) {
        return this.bytes(field, writer.finish());
    }

    finish() {
        return Buffer.concat(this.chunks);
    }
}

function read_varint(cursor) {
    let result = 0n;
    for (let shift = 0n; shift < 70n; shift += 7n) {
        if (cursor.offset >= cursor.buffer.length)
            throw new Error('protobuf: truncated varint');
        const byte = cursor.buffer[cursor.offset++];
        result |= BigInt(byte & 0x7f) << shift;
        if (!(byte & 0x80))
            return result;
    }
    throw new Error('protobuf: varint is too long');
}

function take(cursor, length) {
    if (cursor.offset + length > cursor.buffer.length)
        throw new Error('protobuf: truncated field');
    const value = cursor.buffer.subarray(cursor.offset, cursor.offset + length);
    cursor.offset += length;
    return value;
}

class Message {
    constructor(fields) {
        this.fields = fields;
    }

    last(field, wire_type) {
        const entries = (this.fields.get(field) || []).filter((entry) => entry.wire_type === wire_type);
        return entries.length ? entries[entries.length - 1].value : undefined;
    }

    // uint64 values stay decimal strings; SteamIDs and client ids exceed 2^53.
    uint64(field) {
        const value = this.last(field, WIRE.VARINT) ?? this.last(field, WIRE.FIXED64);
        return value === undefined ? null : BigInt.asUintN(64, value).toString();
    }

    int32(field) {
        const value = this.last(field, WIRE.VARINT);
        return value === undefined ? null : Number(BigInt.asIntN(32, value));
    }

    bool(field) {
        const value = this.last(field, WIRE.VARINT);
        return value === undefined ? null : value !== 0n;
    }

    string(field) {
        const value = this.last(field, WIRE.LENGTH_DELIMITED);
        return value === undefined ? null : value.toString('utf8');
    }

    bytes(field) {
        const value = this.last(field, WIRE.LENGTH_DELIMITED);
        return value === undefined ? null : Buffer.from(value);
    }

    float(field) {
        const value = this.last(field, WIRE.FIXED32);
        return value === undefined ? null : value.readFloatLE(0);
    }

    messages(field) {
        return (this.fields.get(field) || [])
            .filter((entry) => entry.wire_type === WIRE.LENGTH_DELIMITED)
            .map((entry) => decode(entry.value));
    }

    // Accepts both the unpacked encoding proto2 uses by default and the packed one.
    repeated_uint64(field) {
        const values = [];
        for (const entry of this.fields.get(field) || []) {
            if (entry.wire_type === WIRE.VARINT) {
                values.push(BigInt.asUintN(64, entry.value).toString());
            } else if (entry.wire_type === WIRE.LENGTH_DELIMITED) {
                const cursor = { buffer: entry.value, offset: 0 };
                while (cursor.offset < cursor.buffer.length)
                    values.push(BigInt.asUintN(64, read_varint(cursor)).toString());
            }
        }
        return values;
    }
}

function decode(buffer) {
    const cursor = { buffer: Buffer.from(buffer), offset: 0 };
    const fields = new Map();
    while (cursor.offset < cursor.buffer.length) {
        const key = read_varint(cursor);
        const field = Number(key >> 3n);
        const wire_type = Number(key & 7n);
        let value;
        switch (wire_type) {
            case WIRE.VARINT:
                value = read_varint(cursor);
                break;
            case WIRE.FIXED64:
                value = take(cursor, 8).readBigUInt64LE(0);
                break;
            case WIRE.LENGTH_DELIMITED:
                value = take(cursor, Number(read_varint(cursor)));
                break;
            case WIRE.FIXED32:
                value = take(cursor, 4);
                break;
            default:
                throw new Error(`protobuf: unsupported wire type ${wire_type}`);
        }
        if (!fields.has(field))
            fields.set(field, []);
        fields.get(field).push({ wire_type, value });
    }
    return new Message(fields);
}

module.exports = { Writer, decode };
