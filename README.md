# Concurrent Virtual Arena Allocator
This arena allocator pretty closely matches the behavior of my previous [concurrent allocator](https://github.com/chumpitized/concurrent_arena_allocator). That is, I use `fetch_add` as a lock-free way to increment the memory offset in the arena. This new implementation goes a step further by incorporating `VirtualAlloc` to reserve and commit memory inside the arena.

Since this is a concurrent allocator, I needed to find a way to update the memory offset _and_ commit pages without encountering race conditions. Like I said, the offset is handled with a simple `fetch_add` instruction, while the `VirtualAlloc` call uses a compare-and-swap loop.

Here is one way of handling allocations:

```c++
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

		if (a->committed.compare_exchange_weak(committed, committed + to_commit)) {
			VirtualAlloc(&a->buffer[committed], to_commit, MEM_COMMIT, PAGE_READWRITE);
		}
	}

	return &a->buffer[offset];
}
```

This approach looks very similar to the one I used, but it behaves a little differently. Everything relating to `a->curr_offset` is identical, as is the `.load()` of `a->committed` and the use of a CAS loop. The difference lies in _when_ we commit our memory inside that loop. 

In the code above, we wait on a successful update to `a->committed` before calling `VirtualAlloc` to commit new pages. This makes a sort of sense: You want whatever thread won the `compare_exchange_weak` contest to update `a->committed` and commit the correct number of pages. The only issue is that, once you update `a->committed`, any thread is free to allocate memory up to that offset regardless of that memory's status. This means an interrupt could strike at the worst moment — right when `a->committed` changes but before `VirtualAlloc` commits — and permit allocations to uncommitted memory. (This will cause an access violation and kill your process.) 

It might be a fun experiment to think about how you would solve this problem.

Anyway, you have to call `VirtualAlloc` before you update `a->committed` or you're bound to encounter the aforementioned race condition. You simply can't update `a->committed` before you've committed enough memory. With this in mind, the code above transforms into:

```c++
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

		if (a->committed.compare_exchange_weak(committed, committed + to_commit)) {
			break;
		}
	}

	return &a->buffer[offset];
}
```

Now we risk calling `VirtualAlloc` redundantly across threads, but these calls will not fail (so there's no reason to call `VirtualFree`) and their frequency can be reduced by increasing the number of pages commited.
