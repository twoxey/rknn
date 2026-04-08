#pragma once

#include <stdbool.h>
#include <stddef.h>

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a)[0])
#define ARRAY_FOREACH(T, a, item) for (T item = a; item < a + ARRAY_LEN(a); ++item)

void log_print(const char* fmt, ...) __attribute__((format (printf, 1, 2)));
void log_error(const char* fmt, ...) __attribute__((format (printf, 1, 2)));

typedef struct string string;
struct string {
    char* data;
    size_t len;
};
#define stack_buffer(len) ((string){(char[len]){}, len})

string string_from_cstr(const char* cstr);
bool string_eq(string a, string b);
bool string_starts_with(string s, string prefix);
bool string_ends_with(string s, string suffix);
void string_advance(string* s, size_t count);
char* string_find_char(string s, char c);
string string_split_by_char(string* s, char c);
string string_split_by_line(string* s);

typedef struct Allocator Allocator;
struct Allocator {
    void* (*alloc_fn)(void* data, void* ptr, size_t size);
    void* data;
};
static inline void* allocator_alloc(Allocator allocator, size_t size) {
    return allocator.alloc_fn(allocator.data, NULL, size);
}
static inline void allocator_free(Allocator allocator, void* ptr){
    allocator.alloc_fn(allocator.data, ptr, 0);
}

typedef struct Arena Arena;
struct Arena {
    char* base;
    size_t capacity;
    size_t used;
};
#define TEMP_ARENA(size) (Arena){.base = (char[size]){}, .capacity = size}
#define arena_push(a, T) (T*)arena_push_size(a, sizeof(T))
void* arena_push_size(Arena* arena, size_t size);
void* arena_alloc_fn(void* data, void* ptr, size_t size);
static inline Allocator arena_get_allocator(Arena* arena) {
    return (Allocator){arena_alloc_fn, arena};
}

string read_entire_file(const char* file_path, Allocator allocator);

typedef struct Thread Thread;
Thread* thread_start(void* thread_porc(void* data), void* data);
bool thread_join(Thread* thread, void** ret);

//
// Implementation
//

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <pthread.h>

#define log_print(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#define log_error(fmt, ...) fprintf(stderr, "ERROR: " fmt, ## __VA_ARGS__)

string string_from_cstr(const char* cstr) {
    return (string){(char*)cstr, strlen(cstr)};
}

bool string_eq(string a, string b) {
    return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

bool string_starts_with(string s, string prefix) {
    return s.len >= prefix.len && memcmp(s.data, prefix.data, prefix.len) == 0;
}

bool string_ends_with(string s, string suffix) {
    return s.len >= suffix.len && memcmp(s.data + s.len - suffix.len, suffix.data, suffix.len) == 0;
}

void string_advance(string* s, size_t count) {
    assert(count <= s->len);
    s->data += count;
    s->len -= count;
}

char* string_find_char(string s, char c) {
    for (size_t i = 0; i < s.len; ++i) {
        if (s.data[i] == c) return s.data + i;
    }
    return NULL;
}

string string_split_by_char(string* s, char c) {
    string result = {};
    char* lf = string_find_char(*s, c);
    if (!lf) {
        result = *s;
        s->data = NULL;
        s->len = 0;
    } else {
        result.data = s->data;
        result.len = lf - s->data;
        string_advance(s, result.len + 1);
    }
    return result;
}

string string_split_by_line(string* s) {
    string result = string_split_by_char(s, '\n');
    if (result.len > 0 && result.data[result.len - 1] == '\r') --result.len;
    return result;
}

void* arena_push_size(Arena* arena, size_t size) {
    if (arena->used + size > arena->capacity) return NULL;

    void* result = arena->base + arena->used;
    arena->used += size;
    return result;
}

void* arena_alloc_fn(void* data, void* ptr, size_t size) {
    Arena* arena = data;
    if (!ptr) {
        return arena_push_size(arena, size);
    }
    // ignore realloc and free
    return NULL;
}

string read_entire_file(const char* file_path, Allocator allocator) {
    string result = {};
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        log_error("Failed to open file '%s': %s\n", file_path, strerror(errno));
        goto end;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        log_error("fseek() failed: %s\n", strerror(errno));
        goto end;
    }

    long size = ftell(file);
    if (size < 0) {
        log_error("ftell() failed: %s\n", strerror(errno));
        goto end;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        log_error("fseek() failed: %s\n", strerror(errno));
        goto end;
    }

    void* buf = allocator_alloc(allocator, size + 1);
    if (!buf) {
        log_error("Failed to allocate memory\n");
        goto end;
    }

    if (fread(buf, size, 1, file) != 1) {
        log_error("fread() failed: %s\n", strerror(errno));
        goto end;
    }

    result.data = (char*)buf;
    result.len = size;
    result.data[result.len] = '\0';

end:
    if (!result.data) allocator_free(allocator, buf);
    if (file) fclose(file);
    return result;
}

Thread* thread_start(void* thread_porc(void* data), void* data) {
    pthread_t thread;
    int err = pthread_create(&thread, NULL, thread_porc, data);
    if (err) {
        log_error("pthread_create() failed: %s\n", strerror(err));
        return NULL;
    }
    _Static_assert(sizeof(thread) <= sizeof(void*));
    return (void*)thread;
}

bool thread_join(Thread* thread, void** ret) {
    pthread_t pthread = (pthread_t)thread;
    int err = pthread_join(pthread, ret);
    if (err) {
        log_error("pthread_join() failed: %s\n", strerror(err));
        return false;
    }
    return true;
}

