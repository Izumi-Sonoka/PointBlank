#pragma once

/**
 * @file LockFreeStructures.hpp
 * @brief Lock-Free Data Structures for High-Frequency Trading Grade Performance
 * 
 * Implements lock-free data structures optimized for the window manager's
 * event processing pipeline. Designed for sub-microsecond latency and
 * minimal cache misses.
 * 
 * Key optimizations:
 * - Cache-line aligned to prevent false sharing
 * - Memory-mapped I/O for zero-copy operations
 * - Atomic operations with memory ordering optimizations
 * - Bulk operations for batch processing
 * 
 * @author Point Blank Systems Engineering Team
 * @version 2.0.0
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <type_traits>
#include <chrono>
#include <new>
#include <thread>            
#include <xmmintrin.h>       
#include <sys/mman.h>        
#include <unistd.h>          

namespace pblank {
namespace lockfree {

constexpr size_t CACHE_LINE_SIZE = 64;  
constexpr size_t PAGE_SIZE = 4096;       

class SpinWait {
    static constexpr size_t MAX_SPINS = 64;
    size_t spin_count_{0};
    
public:
    void spin() {
        if (spin_count_ < MAX_SPINS) {
            
            for (int i = 0; i < (1 << spin_count_); ++i) {
                _mm_pause();
            }
            ++spin_count_;
        } else {
            
            std::this_thread::yield();
            spin_count_ = 0;
        }
    }
    
    void reset() { spin_count_ = 0; }
    size_t spinCount() const { return spin_count_; }
};

struct MemoryBarrier {
    static void acquire() { std::atomic_thread_fence(std::memory_order_acquire); }
    static void release() { std::atomic_thread_fence(std::memory_order_release); }
    static void seqCst() { std::atomic_thread_fence(std::memory_order_seq_cst); }
};

template<typename T>
class alignas(CACHE_LINE_SIZE) CacheAlignedAtomic {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
    std::atomic<T> value_;
    char padding_[CACHE_LINE_SIZE - sizeof(std::atomic<T>)];
    
public:
    CacheAlignedAtomic() : value_{} {}
    explicit CacheAlignedAtomic(T val) : value_(val) {}
    
    T load(std::memory_order order = std::memory_order_seq_cst) const {
        return value_.load(order);
    }
    
    void store(T val, std::memory_order order = std::memory_order_seq_cst) {
        value_.store(val, order);
    }
    
    T exchange(T val, std::memory_order order = std::memory_order_seq_cst) {
        return value_.exchange(val, order);
    }
    
    bool compareExchangeWeak(T& expected, T desired,
                             std::memory_order success = std::memory_order_seq_cst,
                             std::memory_order failure = std::memory_order_seq_cst) {
        return value_.compare_exchange_weak(expected, desired, success, failure);
    }
    
    bool compareExchangeStrong(T& expected, T desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) {
        return value_.compare_exchange_strong(expected, desired, success, failure);
    }
    
    T fetchAdd(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_add(arg, order);
    }
    
    T fetchSub(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_sub(arg, order);
    }
};

template<typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    alignas(64) T buffer_[Capacity];
    
    static constexpr size_t MASK = Capacity - 1;
    
public:
    SPSCRingBuffer() = default;
    
    bool push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & MASK;
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  
        }
        
        buffer_[tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    bool push(T&& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & MASK;
        
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  
        }
        
        buffer_[tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }
    
    std::optional<T> pop() {
        const size_t head = head_.load(std::memory_order_relaxed);
        
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;  
        }
        
        T item = buffer_[head];
        head_.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }
    
    std::optional<T> peek() const {
        const size_t head = head_.load(std::memory_order_relaxed);
        
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        
        return buffer_[head];
    }
    
    size_t size() const {
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t head = head_.load(std::memory_order_acquire);
        return (tail - head + Capacity) & MASK;
    }
    
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    bool full() const {
        const size_t next_tail = (tail_.load(std::memory_order_relaxed) + 1) & MASK;
        return next_tail == head_.load(std::memory_order_acquire);
    }
    
    static constexpr size_t capacity() { return Capacity; }
};

template<typename T>
class MPSCQueue {
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
        
        template<typename... Args>
        explicit Node(Args&&... args) : data(std::forward<Args>(args)...) {}
    };
    
    alignas(CACHE_LINE_SIZE) std::atomic<Node*> head_;
    alignas(CACHE_LINE_SIZE) Node* tail_;
    
public:
    MPSCQueue() {
        Node* stub = new Node();
        head_.store(stub, std::memory_order_relaxed);
        tail_ = stub;
    }
    
    ~MPSCQueue() {
        while (pop()) {}
        delete tail_;  
    }
    
    void push(const T& item) {
        Node* node = new Node(item);
        Node* prev = head_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }
    
    void push(T&& item) {
        Node* node = new Node(std::move(item));
        Node* prev = head_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }
    
    std::optional<T> pop() {
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return std::nullopt;
        }
        
        T item = std::move(next->data);
        tail_->next.store(nullptr, std::memory_order_release);
        delete tail_;
        tail_ = next;
        
        return item;
    }
    
    bool empty() const {
        return tail_->next.load(std::memory_order_acquire) == nullptr;
    }
};

template<typename T>
class WorkStealingDeque {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
    struct Array {
        int64_t capacity;
        std::atomic<T>* buffer;  
        
        static Array* create(int64_t cap) {
            Array* arr = static_cast<Array*>(std::aligned_alloc(
                CACHE_LINE_SIZE, sizeof(Array)));
            if (arr) {
                arr->capacity = cap;
                arr->buffer = static_cast<std::atomic<T>*>(
                    std::aligned_alloc(CACHE_LINE_SIZE, cap * sizeof(std::atomic<T>)));
                if (!arr->buffer) {
                    std::free(arr);
                    return nullptr;
                }
            }
            return arr;
        }
        
        static void destroy(Array* arr) {
            if (arr) {
                if (arr->buffer) {
                    std::free(arr->buffer);
                }
                std::free(arr);
            }
        }
    };
    
    alignas(CACHE_LINE_SIZE) std::atomic<int64_t> top_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<int64_t> bottom_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<Array*> array_;
    
public:
    WorkStealingDeque(int64_t initial_capacity = 1024) {
        array_.store(Array::create(initial_capacity), std::memory_order_relaxed);
    }
    
    ~WorkStealingDeque() {
        Array::destroy(array_.load(std::memory_order_relaxed));
    }
    
    void push(T item) {
        int64_t bottom = bottom_.load(std::memory_order_relaxed);
        int64_t top = top_.load(std::memory_order_acquire);
        Array* arr = array_.load(std::memory_order_relaxed);
        
        if (bottom - top > arr->capacity - 1) {
            
            Array* new_arr = Array::create(arr->capacity * 2);
            for (int64_t i = top; i < bottom; ++i) {
                new_arr->buffer[i % new_arr->capacity].store(
                    arr->buffer[i % arr->capacity].load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
            }
            Array* old_arr = arr;
            array_.store(new_arr, std::memory_order_relaxed);
            Array::destroy(old_arr);
            arr = new_arr;
        }
        
        arr->buffer[bottom % arr->capacity].store(item, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(bottom + 1, std::memory_order_relaxed);
    }
    
    std::optional<T> pop() {
        int64_t bottom = bottom_.load(std::memory_order_relaxed) - 1;
        Array* arr = array_.load(std::memory_order_relaxed);
        bottom_.store(bottom, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        
        int64_t top = top_.load(std::memory_order_relaxed);
        
        if (top <= bottom) {
            T item = arr->buffer[bottom % arr->capacity].load(std::memory_order_relaxed);
            
            if (top == bottom) {
                
                if (!top_.compare_exchange_strong(top, top + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    return std::nullopt;  
                }
                bottom_.store(bottom + 1, std::memory_order_relaxed);
            }
            return item;
        } else {
            bottom_.store(bottom + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }
    
    std::optional<T> steal() {
        int64_t top = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t bottom = bottom_.load(std::memory_order_acquire);
        
        if (top < bottom) {
            Array* arr = array_.load(std::memory_order_consume);
            T item = arr->buffer[top % arr->capacity].load(std::memory_order_relaxed);
            
            if (top_.compare_exchange_strong(top, top + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return item;
            }
        }
        return std::nullopt;
    }
    
    bool empty() const {
        return top_.load(std::memory_order_acquire) >= 
               bottom_.load(std::memory_order_acquire);
    }
    
    int64_t size() const {
        return bottom_.load(std::memory_order_acquire) - 
               top_.load(std::memory_order_acquire);
    }
};

template<typename T, size_t Capacity>
class MMapRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    
    struct Header {
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail{0};
        size_t capacity{Capacity};
        uint32_t version{1};
        uint32_t checksum{0};
    };
    
    void* mapped_region_;
    Header* header_;
    T* buffer_;
    
    static constexpr size_t MASK = Capacity - 1;
    
public:
    
    MMapRingBuffer(int fd, bool create = true) {
        const size_t total_size = sizeof(Header) + Capacity * sizeof(T);
        
        if (create) {
            if (ftruncate(fd, total_size) < 0) {
                throw std::system_error(errno, std::system_category(), "ftruncate failed");
            }
        }
        
        mapped_region_ = mmap(nullptr, total_size, 
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        
        if (mapped_region_ == MAP_FAILED) {
            throw std::system_error(errno, std::system_category(), "mmap failed");
        }
        
        header_ = static_cast<Header*>(mapped_region_);
        buffer_ = reinterpret_cast<T*>(static_cast<char*>(mapped_region_) + sizeof(Header));
        
        if (create) {
            new (header_) Header();
        }
    }
    
    ~MMapRingBuffer() {
        const size_t total_size = sizeof(Header) + Capacity * sizeof(T);
        munmap(mapped_region_, total_size);
    }
    
    bool write(const T& item) {
        const size_t tail = header_->tail.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) & MASK;
        
        if (next_tail == header_->head.load(std::memory_order_acquire)) {
            return false;  
        }
        
        buffer_[tail] = item;
        header_->tail.store(next_tail, std::memory_order_release);
        return true;
    }
    
    std::optional<T> read() {
        const size_t head = header_->head.load(std::memory_order_relaxed);
        
        if (head == header_->tail.load(std::memory_order_acquire)) {
            return std::nullopt;  
        }
        
        T item = buffer_[head];
        header_->head.store((head + 1) & MASK, std::memory_order_release);
        return item;
    }
    
    size_t size() const {
        const size_t tail = header_->tail.load(std::memory_order_acquire);
        const size_t head = header_->head.load(std::memory_order_acquire);
        return (tail - head + Capacity) & MASK;
    }
    
    bool empty() const {
        return header_->head.load(std::memory_order_acquire) == 
               header_->tail.load(std::memory_order_acquire);
    }
};

class AtomicBitfield {
    std::atomic<uint64_t> bits_{0};
    
public:
    bool test(size_t pos) const {
        return bits_.load(std::memory_order_acquire) & (1ULL << pos);
    }
    
    void set(size_t pos) {
        bits_.fetch_or(1ULL << pos, std::memory_order_release);
    }
    
    void clear(size_t pos) {
        bits_.fetch_and(~(1ULL << pos), std::memory_order_release);
    }
    
    void toggle(size_t pos) {
        bits_.fetch_xor(1ULL << pos, std::memory_order_acq_rel);
    }
    
    bool testAndSet(size_t pos) {
        uint64_t bit = 1ULL << pos;
        uint64_t old = bits_.fetch_or(bit, std::memory_order_acq_rel);
        return old & bit;
    }
    
    uint64_t get() const {
        return bits_.load(std::memory_order_acquire);
    }
    
    void setAll(uint64_t value) {
        bits_.store(value, std::memory_order_release);
    }
};

class SequenceLock {
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> sequence_{0};
    
public:
    
    uint32_t readBegin() const {
        uint32_t seq;
        while ((seq = sequence_.load(std::memory_order_acquire)) & 1) {
            _mm_pause();  
        }
        return seq;
    }
    
    bool readValidate(uint32_t seq) const {
        std::atomic_thread_fence(std::memory_order_acquire);
        return sequence_.load(std::memory_order_relaxed) == seq;
    }
    
    void writeBegin() {
        uint32_t seq = sequence_.load(std::memory_order_relaxed);
        while (!sequence_.compare_exchange_weak(seq, seq + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            _mm_pause();
        }
    }
    
    void writeEnd() {
        sequence_.fetch_add(1, std::memory_order_release);
    }
    
    class WriteGuard {
        SequenceLock& lock_;
    public:
        explicit WriteGuard(SequenceLock& lock) : lock_(lock) { lock_.writeBegin(); }
        ~WriteGuard() { lock_.writeEnd(); }
    };
    
    WriteGuard writeLock() { return WriteGuard(*this); }
};

template<typename T, size_t PoolSize>
class ObjectPool {
    union Block {
        T object;
        Block* next;
        
        Block() {}  
        ~Block() {}
    };
    
    alignas(CACHE_LINE_SIZE) Block blocks_[PoolSize];
    alignas(CACHE_LINE_SIZE) std::atomic<Block*> free_list_{nullptr};
    
public:
    ObjectPool() {
        
        for (size_t i = 0; i < PoolSize - 1; ++i) {
            blocks_[i].next = &blocks_[i + 1];
        }
        blocks_[PoolSize - 1].next = nullptr;
        free_list_.store(&blocks_[0], std::memory_order_relaxed);
    }
    
    template<typename... Args>
    T* allocate(Args&&... args) {
        Block* block = free_list_.load(std::memory_order_acquire);
        
        while (block && !free_list_.compare_exchange_weak(block, block->next,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            _mm_pause();
        }
        
        if (!block) {
            return nullptr;  
        }
        
        return new (&block->object) T(std::forward<Args>(args)...);
    }
    
    void deallocate(T* obj) {
        Block* block = reinterpret_cast<Block*>(obj);
        obj->~T();  
        
        Block* old_head = free_list_.load(std::memory_order_relaxed);
        do {
            block->next = old_head;
        } while (!free_list_.compare_exchange_weak(old_head, block,
                std::memory_order_acq_rel, std::memory_order_relaxed));
    }
    
    size_t available() const {
        size_t count = 0;
        Block* block = free_list_.load(std::memory_order_acquire);
        while (block) {
            ++count;
            block = block->next;
        }
        return count;
    }
};

} 
} 
