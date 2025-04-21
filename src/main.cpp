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
	std::atomic<size_t> committed;
	std::atomic<size_t> curr_offset;
};

void arena_init(Arena *a, size_t length) {
	a->buffer = (unsigned char *)VirtualAlloc(NULL, length, MEM_RESERVE, PAGE_READWRITE);
	a->length = length;
	a->curr_offset.store(0);
}

inline bool is_power_of_two(size_t x) {
	return (x & (x - 1)) == 0;
}

inline size_t align_forward(size_t size, size_t align) {
	return (size + align - 1) & ~(align - 1);
}

inline size_t commit_size(size_t committed, size_t allocated) {
	size_t diff = allocated - committed;
	return (diff + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void *arena_concurrent_alloc(Arena *a, size_t size, size_t align) {
	size_t aligned_size = align_forward(size, align);
	size_t offset = a->curr_offset.fetch_add(aligned_size);
	size_t allocated = offset + size;
	
	if (allocated > a->length) {
		a->curr_offset.fetch_sub(aligned_size);
		return NULL;
	}

	size_t committed = a->committed.load();

	while (true) {
		if (allocated <= committed) break;
		size_t to_commit = commit_size(committed, allocated);

		VirtualAlloc(&a->buffer[committed], to_commit, MEM_COMMIT, PAGE_READWRITE);

		if (a->committed.compare_exchange_weak(committed, to_commit)) {
			break;
		}
	}

	return &a->buffer[offset];
}

void arena_clear(Arena *a) {
	a->curr_offset.store(0);
}

void allocate(Arena *a) {
	for (int i = 0; i < 1024; ++i) {
		std::cout << "Allocating int from thread ID: " << std::this_thread::get_id() << std::endl;
		void *ptr = arena_concurrent_alloc(a, 4, DEFAULT_ALIGNMENT);
		//To make sure the memory is actually committed and not just reserved, we allocate an int.
		*(int *)ptr = 4;
		std::cout << "Current offset is " << a->curr_offset.load() << std::endl;
	}
}

int main() {
	size_t mb = 1024 * 1024;
	Arena *a = new Arena;
	arena_init(a, mb);

	std::thread t1(allocate, a);
	std::thread t2(allocate, a);
	std::thread t3(allocate, a);

	t1.join();
	t2.join();
	t3.join();

	return 0;
}