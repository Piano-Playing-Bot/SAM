// Different types of Allocators
//
// Define ALLOC_IMPLEMENTATION in some file, to include the function bodies
// Drop-in replacement for malloc/free via the global alloc_ctx variable
// Call alloc_ctxAlloc, alloc_ctxFree, alloc_ctxFreeAll to use whichever allocator is currently set as the context
// Use ALLOC_SWITCH_CTX and ALLOC_SWITCH_BACK_CTX to change the context (back)

#ifndef _ALLOC_H_
#define _ALLOC_H_

#include <stdint.h>
#include "util.h"   // For macros

typedef struct {
	void *data;
	void *(*alloc)(void*, size_t);
    void  (*free)(void*, void*);
    void  (*freeAll)(void*);
} Alloc_Ctx_Allocator;

// @TODO: This allocator isn't really an arena -> need another name
typedef struct {
	size_t cap;
	size_t perm;
	size_t tmp;
} Alloc_Arena_Header;

void *alloc_ctxAlloc(size_t size);
void  alloc_ctxFree(void *ptr);
void  alloc_ctxFreeAll();
void *alloc_stdAlloc(void *data, size_t size);
void  alloc_stdFree(void *data, void *ptr);
void  alloc_stdFreeAll(void *data);
void *alloc_arenaAllocPerm(void *data, size_t size);
void *alloc_arenaAllocTmp(void *data, size_t size);
void  alloc_arenaFree(void *data, void *ptr);
void  alloc_arenaFreeAll(void *data);

static Alloc_Ctx_Allocator alloc_std = {
	.data    = NULL,
	.alloc   = alloc_stdAlloc,
	.free    = alloc_stdFree,
	.freeAll = alloc_stdFreeAll,
};

static Alloc_Ctx_Allocator *alloc_ctx = &alloc_std;
static Alloc_Ctx_Allocator *_alloc_prev_ctx_ = NULL;
#define ALLOC_SWITCH_CTX(allocator) { _alloc_prev_ctx_ = alloc_ctx; alloc_ctx = (allocator); }
#define ALLOC_SWITCH_BACK_CTX() alloc_ctx = _alloc_prev_ctx_
#define ALLOC_GET_HEADER(strct, data) ((strct *)(((char *)(data))-sizeof(strct)))


#endif // _ALLOC_H

#ifdef ALLOC_IMPLEMENTATION
#ifndef _ALLOC_IMPL_GUARD_
#define _ALLOC_IMPL_GUARD_

#include <stdlib.h> // For std_allocator

/////////////
// Context //
/////////////

void *alloc_ctxAlloc(size_t size)
{
	return alloc_ctx->alloc(alloc_ctx->data, size);
}

void alloc_ctxFree(void *ptr)
{
	alloc_ctx->free(alloc_ctx->data, ptr);
}

void alloc_ctxFreeAll()
{
	alloc_ctx->freeAll(alloc_ctx->data);
}


/////////
// Std //
/////////

void *alloc_stdAlloc(void *data, size_t size)
{
	(void)data;
	return malloc(size);
}

void alloc_stdFree(void *data, void *ptr)
{
	(void)data;
	free(ptr);
}

void alloc_stdFreeAll(void *data)
{
	(void)data;
}


///////////
// Arena //
///////////

// @Note: Uses current context as backing allocator
// @Note: This allocator stores two buffers - one permament, that is never freed - and one temporary, that is only freed on freeAll
// @Note: Calling free is a no-op
// @Note: The two buffers grow towards each other (like stack & heap)
Alloc_Ctx_Allocator alloc_arenaInit(Alloc_Ctx_Allocator allocator, size_t initialSize)
{
	void *data = allocator.alloc(allocator.data, sizeof(Alloc_Arena_Header) + initialSize);
	*((Alloc_Arena_Header *)data) = (Alloc_Arena_Header) {
		.cap  = initialSize,
		.perm = 0,
		.tmp  = 0,
	};
	return (Alloc_Ctx_Allocator) {
		.data    = data + sizeof(Alloc_Arena_Header),
		.alloc   = alloc_arenaAllocTmp,
		.free    = alloc_arenaFree,
		.freeAll = alloc_arenaFreeAll,
	};
}

// @TODO: We could avoid returning NULL, by making the arena into a linked-list
// We would need to add at least two fields to the header then:
// 1. A pointer at the backing allocator from alloc_arenaInit
// 2. A pointer to the next Arena buffer
void *alloc_arenaAllocPerm(void *data, size_t size)
{
	Alloc_Arena_Header *header = ALLOC_GET_HEADER(Alloc_Arena_Header, data);
	if (UTIL_UNLIKELY(header->perm + header->tmp + size >= header->cap)) return NULL;
	void *out = &data[header->perm];
	header->perm += size;
	return out;
}

void *alloc_arenaAllocTmp(void *data, size_t size)
{
	Alloc_Arena_Header *header = ALLOC_GET_HEADER(Alloc_Arena_Header, data);
	if (UTIL_UNLIKELY(header->perm + header->tmp + size >= header->cap)) return NULL;
	void *out = &data[header->cap - header->tmp - size - 1];
	header->tmp += size;
	return out;
}

void  alloc_arenaFree(void *data, void *ptr)
{
	(void)data;
	(void)ptr;
}

void  alloc_arenaFreeAll(void *data)
{
	Alloc_Arena_Header *header = ALLOC_GET_HEADER(Alloc_Arena_Header, data);
	header->tmp = 0;
	// @Decide: Should we memset previously allocated region to 0?
}

#endif // _ALLOC_IMPL_GUARD_
#endif // ALLOC_IMPLEMENTATION