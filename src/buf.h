#ifndef BUF_H_
#define BUF_H_

#include "util.h"

#if !defined(BUF_FREE) && !defined(BUF_MALLOC)
#include <stdlib.h>
#define BUF_FREE(ptr)    free(ptr)
#define BUF_MALLOC(size) malloc(size)
#elif !defined(BUF_FREE) || !defined(BUF_MALLOC)
#error "You must define both BUF_MALLOC and BUF_FREE, or neither."
#elif !defined(UTIL_MALLOC)
#define UTIL_MALLOC(size) BUF_MALLOC(size)
#endif

typedef struct {
	u8 *data;
	u64 idx;
	u64 size;
	u64 cap;
} Buffer;

// @TODO: Add overflow checks when reading/peeking

Buffer buf_fromFile(const char *filename);
Buffer buf_fromData(u8 *data, u64 size, u64 idx);
bool buf_copyToFile(Buffer buf, const char *filename);
bool buf_toFile(Buffer *buf, const char *filename);
Buffer buf_new(u64 cap);
void buf_ensure_size(Buffer *buf, u64 n);
void buf_free(Buffer buf);
u8  buf_peek1   (Buffer  buf);
u16 buf_peek2lsb(Buffer  buf);
u32 buf_peek4lsb(Buffer  buf);
u64 buf_peek8lsb(Buffer  buf);
u16 buf_peek2msb(Buffer  buf);
u32 buf_peek4msb(Buffer  buf);
u64 buf_peek8msb(Buffer  buf);
u8  buf_read1   (Buffer *buf);
u16 buf_read2lsb(Buffer *buf);
u32 buf_read4lsb(Buffer *buf);
u64 buf_read8lsb(Buffer *buf);
u16 buf_read2msb(Buffer *buf);
u32 buf_read4msb(Buffer *buf);
u64 buf_read8msb(Buffer *buf);
void buf_write1   (Buffer *buf, u8  val);
void buf_write2lsb(Buffer *buf, u16 val);
void buf_write4lsb(Buffer *buf, u32 val);
void buf_write8lsb(Buffer *buf, u64 val);
void buf_write2msb(Buffer *buf, u16 val);
void buf_write4msb(Buffer *buf, u32 val);
void buf_write8msb(Buffer *buf, u64 val);
#endif // BUF_H_


#define BUF_IMPLEMENTATION
#ifdef BUF_IMPLEMENTATION

Buffer buf_fromFile(const char *filename)
{
	u64 size;
	u8 *data = (u8 *)util_readFile(filename, &size);
	return (Buffer) {
		.data = data,
		.idx  = 0,
		.size = size,
		.cap  = size,
	};
}

inline Buffer buf_fromData(u8 *data, u64 size, u64 idx)
{
	return (Buffer) {
		.data = data,
		.size = size,
		.cap  = size,
		.idx  = idx,
	};
}

inline bool buf_copyToFile(Buffer buf, const char *filename)
{
	return util_writeFile(filename, (char*) buf.data, buf.size);
}

// Unlike copyToFile, this function frees the buffer after writing it to the file
bool buf_toFile(Buffer *buf, const char *filename)
{
	bool out = util_writeFile(filename, (char*)(buf->data), buf->size);
	BUF_FREE((void *)buf->data);
	return out;
}

Buffer buf_new(u64 cap)
{
	Buffer buf = {
		.data = BUF_MALLOC(cap),
		.size = 0,
		.cap  = cap,
		.idx  = 0
	};
	return buf;
}

inline void buf_free(Buffer buf)
{
	BUF_FREE((void *)buf.data);
}

// Ensures that there's enough capacity to write `n` more bytes into the buffer
void buf_ensure_size(Buffer *buf, u64 n)
{
	u64 min = buf->size + n;
	if (UTIL_UNLIKELY(min > buf->cap)) {
		u64 new_cap = buf->cap * 2;
		if (UTIL_UNLIKELY(min > new_cap)) new_cap = min;
		u8 *new_data = BUF_MALLOC(new_cap);
		memcpy(new_data, buf->data, buf->size);
		BUF_FREE(buf->data);
		buf->data = new_data;
		buf->cap = new_cap;
	}
}

inline u8  buf_peek1(Buffer buf)
{
	return buf.data[buf.idx];
}
#define buf_peek1lsb(buf) buf_peek1(buf)
#define buf_peek1msb(buf) buf_peek1(buf)

inline u16 buf_peek2lsb(Buffer buf)
{
	return ((u16)buf.data[buf.idx + 1] << 8) | ((u16)buf.data[buf.idx + 0] << 0);
}

inline u32 buf_peek4lsb(Buffer buf)
{
	return ((u32)buf.data[buf.idx + 3] << 24) | ((u32)buf.data[buf.idx + 2] << 16) | ((u32)buf.data[buf.idx + 1] << 8) | ((u32)buf.data[buf.idx + 0] << 0);
}

inline u64 buf_peek8lsb(Buffer buf)
{
	return ((u64)buf.data[buf.idx + 7] << 56) | ((u64)buf.data[buf.idx + 6] << 48) | ((u64)buf.data[buf.idx + 5] << 40) | ((u64)buf.data[buf.idx + 4] << 32) |
		   ((u64)buf.data[buf.idx + 3] << 24) | ((u64)buf.data[buf.idx + 2] << 16) | ((u64)buf.data[buf.idx + 1] <<  8) | ((u64)buf.data[buf.idx + 0] <<  0);
}

inline u16 buf_peek2msb(Buffer buf)
{
	return ((u16)buf.data[buf.idx + 0] << 8) | ((u16)buf.data[buf.idx + 1] << 0);
}

inline u32 buf_peek4msb(Buffer buf)
{
	return ((u32)buf.data[buf.idx + 0] << 24) | ((u32)buf.data[buf.idx + 1] << 16) | ((u32)buf.data[buf.idx + 2] << 8) | ((u32)buf.data[buf.idx + 3] << 0);
}

inline u64 buf_peek8msb(Buffer buf)
{
	return ((u64)buf.data[buf.idx + 0] << 56) | ((u64)buf.data[buf.idx + 1] << 48) | ((u64)buf.data[buf.idx + 2] << 40) | ((u64)buf.data[buf.idx + 3] << 32) |
		   ((u64)buf.data[buf.idx + 4] << 24) | ((u64)buf.data[buf.idx + 5] << 16) | ((u64)buf.data[buf.idx + 6] <<  8) | ((u64)buf.data[buf.idx + 7] <<  0);
}

u8  buf_read1(Buffer *buf)
{
	return buf->data[buf->idx++];
}
#define buf_read1lsb(buf) buf_read1(buf)
#define buf_read1msb(buf) buf_read1(buf)

u16 buf_read2lsb(Buffer *buf)
{
	u16 out   = buf_peek2lsb(*buf);
	buf->idx += sizeof(u16);
	return out;
}

u32 buf_read4lsb(Buffer *buf)
{
	u32 out   = buf_peek4lsb(*buf);
	buf->idx += sizeof(u32);
	return out;
}

u64 buf_read8lsb(Buffer *buf)
{
	u64 out   = buf_peek8lsb(*buf);
	buf->idx += sizeof(u64);
	return out;
}

u16 buf_read2msb(Buffer *buf)
{
	u16 out   = buf_peek2msb(*buf);
	buf->idx += sizeof(u16);
	return out;
}

u32 buf_read4msb(Buffer *buf)
{
	u32 out   = buf_peek4msb(*buf);
	buf->idx += sizeof(u32);
	return out;
}

u64 buf_read8msb(Buffer *buf)
{
	u64 out   = buf_peek8msb(*buf);
	buf->idx += sizeof(u64);
	return out;
}

void buf_write1(Buffer *buf, u8  val)
{
	buf_ensure_size(buf, 2);
	buf->data[buf->idx++] = val;
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}
#define buf_write1lsb(buf, val) buf_write1(buf, val)
#define buf_write1msb(buf, val) buf_write1(buf, val)

void buf_write2lsb(Buffer *buf, u16 val)
{
	buf_ensure_size(buf, sizeof(u16));
	for (u8 i = 0; i < (u8)sizeof(u16); i++) {
		buf->data[buf->idx++] = (u8)(val & 0xff);
		val >>= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}

void buf_write4lsb(Buffer *buf, u32 val)
{
	buf_ensure_size(buf, sizeof(u32));
	for (u8 i = 0; i < (u8)sizeof(u32); i++) {
		buf->data[buf->idx++] = (u8)(val & 0xff);
		val >>= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}

void buf_write8lsb(Buffer *buf, u64 val)
{
	buf_ensure_size(buf, sizeof(u64));
	for (u8 i = 0; i < (u8)sizeof(u64); i++) {
		buf->data[buf->idx++] = (u8)(val & 0xff);
		val >>= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}

void buf_write2msb(Buffer *buf, u16 val)
{
	buf_ensure_size(buf, sizeof(u16));
	for (u8 i = 0; i < (u8)sizeof(u16); i++) {
		buf->data[buf->idx++] = (u8)(val & (0xff << 8*(sizeof(u16) - 1)));
		val <<= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}

void buf_write4msb(Buffer *buf, u32 val)
{
	buf_ensure_size(buf, sizeof(u32));
	for (u8 i = 0; i < (u8)sizeof(u32); i++) {
		buf->data[buf->idx++] = (u8)(val & (0xff << 8*(sizeof(u32) - 1)));
		val <<= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}

void buf_write8msb(Buffer *buf, u64 val)
{
	buf_ensure_size(buf, sizeof(u64));
	for (u8 i = 0; i < (u8)sizeof(u64); i++) {
		buf->data[buf->idx++] = (u8)(val & ((u64)0xff << 8*(sizeof(u64) - 1)));
		val <<= 8;
	}
	if (UTIL_LIKELY(buf->idx > buf->size)) buf->size = buf->idx;
}


#endif // BUF_IMPLEMENTATION