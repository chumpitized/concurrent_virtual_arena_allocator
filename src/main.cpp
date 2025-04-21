#include <assert.h>
#include <cstring>
#include <iostream>
#include <windows.h>
#include <atomic>
#include <thread>

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT sizeof(void *)
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

typedef uintptr_t uptr;

typedef struct Arena Arena;
struct Arena {
	unsigned char *buffer;
	size_t length;
	std::atomic<size_t> curr_offset;
};

void arena_init(Arena *a, void *buffer, size_t length) {
	a->buffer = (unsigned char *)buffer;
	a->length = length;
	a->curr_offset.store(0);
}

inline bool is_power_of_two(size_t x) {
	return (x & (x - 1)) == 0;
}

inline size_t align_forward(size_t size, size_t align) {
	return (size + align - 1) & ~(align - 1);
}

void *arena_concurrent_alloc(Arena *a, size_t size, size_t align) {
	size_t aligned_size = align_forward(size, align);
	size_t offset = a->curr_offset.fetch_add(aligned_size);
	
	if (offset + size > a->length) {
		a->curr_offset.fetch_sub(aligned_size);
		return NULL;
	}

	return &a->buffer[offset];
}

void arena_clear(Arena *a) {
	a->curr_offset.store(0);
}

void allocate(Arena *a) {
	for (int i = 0; i < 1024; ++i) {
		std::cout << "Allocating int from thread ID: " << std::this_thread::get_id() << std::endl;
		arena_concurrent_alloc(a, 4, DEFAULT_ALIGNMENT);
		std::cout << "Current offset is " << a->curr_offset.load() << std::endl;
	}
}

int main() {
	size_t mb = 1024 * 1024;
	void *buffer = malloc(mb);
	Arena *a = new Arena;
	arena_init(a, buffer, mb);

	std::thread t1(allocate, a);
	std::thread t2(allocate, a);
	std::thread t3(allocate, a);

	t1.join();
	t2.join();
	t3.join();

	return 0;
}