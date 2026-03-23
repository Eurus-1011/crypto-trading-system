#pragma once

#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

template <typename EntryT, size_t Capacity = 4096>
struct RingShm {
    std::atomic<uint64_t> head{0};
    std::atomic<uint64_t> tail{0};
    EntryT entries[Capacity];

    static constexpr size_t capacity = Capacity;
    static constexpr size_t shm_size() { return sizeof(RingShm); }
};

template <typename EntryT, size_t Capacity>
inline RingShm<EntryT, Capacity>* shm_create(const char* name) {
    shm_unlink(name);
    size_t sz = RingShm<EntryT, Capacity>::shm_size();
    int fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, static_cast<off_t>(sz)) != 0) {
        close(fd);
        shm_unlink(name);
        return nullptr;
    }
    void* p = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        shm_unlink(name);
        return nullptr;
    }
    std::memset(p, 0, sz);
    return static_cast<RingShm<EntryT, Capacity>*>(p);
}

template <typename EntryT, size_t Capacity>
inline RingShm<EntryT, Capacity>* shm_attach(const char* name) {
    size_t sz = RingShm<EntryT, Capacity>::shm_size();
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return nullptr;
    void* p = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return nullptr;
    return static_cast<RingShm<EntryT, Capacity>*>(p);
}

template <typename EntryT, size_t Capacity>
inline void shm_detach(RingShm<EntryT, Capacity>* r) {
    if (r) munmap(r, RingShm<EntryT, Capacity>::shm_size());
}

template <typename EntryT, size_t Capacity>
inline void shm_destroy(const char* name, RingShm<EntryT, Capacity>* r) {
    if (r) munmap(r, RingShm<EntryT, Capacity>::shm_size());
    shm_unlink(name);
}

template <typename EntryT, size_t Capacity>
inline bool shm_push(RingShm<EntryT, Capacity>* r, const EntryT& e) {
    uint64_t slot = r->tail.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= r->head.load(std::memory_order_acquire) + Capacity) {
        r->tail.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    r->entries[slot % Capacity] = e;
    std::atomic_thread_fence(std::memory_order_release);
    return true;
}

template <typename EntryT, size_t Capacity>
inline bool shm_pop(RingShm<EntryT, Capacity>* r, EntryT& e) {
    uint64_t slot = r->head.fetch_add(1, std::memory_order_acq_rel);
    if (slot >= r->tail.load(std::memory_order_acquire)) {
        r->head.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    e = r->entries[slot % Capacity];
    return true;
}
