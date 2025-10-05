#pragma once
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <memory>

/**
 * @brief ### Per-entry reader-writer lock with writer preference (prevents writer starvation).
 * 
 * Each Entry in LockedMap has its own independent lock, enabling fine-grained concurrency.
 * Multiple threads can read the same entry simultaneously, but writes are exclusive.
 * 
 * Writer preference policy:
 * - Readers wait if ANY writers are waiting (prevents writer starvation)
 * - Writers wait only for active readers to finish
 * - Without this, continuous readers could starve writers indefinitely
 * 
 * Lock states:
 * - active_readers > 0, writer_active = false: Multiple readers active
 * - active_readers = 0, writer_active = true: Single writer active
 * - Both = 0: Entry unlocked, available for locking
 * 
 * @tparam V Type of the stored value (can be any copyable type).
 */
template<typename V>
struct Entry {
    V value;                        ///< The actual stored data (protected by locks below)
    
    // ===== Reader-writer lock state =====
    uint32_t active_readers = 0;    ///< Number of threads currently holding read lock (can be > 1)
    bool writer_active = false;     ///< True if a thread currently holds write lock (exclusive, max 1)
    uint32_t waiting_writers = 0;   ///< Number of threads waiting to acquire write lock (for priority)
    
    // ===== Synchronization primitives =====
    std::mutex mutex;               ///< Protects the lock state variables (active_readers, writer_active, etc.)
    std::condition_variable cv;     ///< Signals when lock state changes (wakes waiting readers/writers)

    /**
     * @brief ### Acquires read lock (shared, multiple readers allowed).
     * 
     * Blocks if:
     * - A writer is currently active (writer_active = true)
     * - Any writers are waiting (waiting_writers > 0, writer preference policy)
     * 
     * Multiple readers can hold locks simultaneously (active_readers incremented).
     * Read operations don't modify data, so concurrent reads are safe.
     */
    void lock_read();

    /**
     * @brief ### Releases read lock.
     * 
     * Decrements active_readers counter.
     * If this was the last reader (active_readers becomes 0), notifies waiting writers.
     */
    void unlock_read();

    /**
     * @brief ### Acquires write lock (exclusive, only one writer allowed).
     * 
     * Blocks if:
     * - A writer is currently active (writer_active = true)
     * - Any readers are active (active_readers > 0)
     * 
     * Increments waiting_writers before waiting (blocks new readers, writer preference).
     * Only one writer can hold lock at a time (exclusive access for modification).
     */
    void lock_write();

    /**
     * @brief ### Releases write lock.
     * 
     * Sets writer_active = false and notifies ALL waiting threads.
     * Both readers and writers wake up and compete for next lock acquisition.
     */
    void unlock_write();
};

/**
 * @brief ### Thread-safe map with per-entry reader-writer locks.
 * 
 * Provides fine-grained locking for concurrent access to individual map entries.
 * Unlike a global map mutex, different entries can be locked independently.
 * 
 * Concurrency guarantees:
 * - Multiple threads can read/write different entries simultaneously (fine-grained locking)
 * - Multiple threads can read the same entry simultaneously (reader-writer lock)
 * - Only one thread can write to an entry at a time (exclusive write access)
 * - Map structure modifications (inserts) use separate map_mutex (coarse-grained)
 * 
 * Deadlock prevention:
 * - atomic_pair_operation() locks entries in fixed order (by pointer address)
 * - Prevents circular wait condition (AB-BA deadlock)
 * 
 * Use case: Server's client map where transactions lock 2 entries simultaneously.
 * 
 * @tparam K Key type (must be hashable for unordered_map).
 * @tparam V Value type (must be copyable for read() operation).
 */
template<typename K, typename V>
class LockedMap {
public:
    /**
     * @brief ### Inserts a new key-value pair if key doesn't exist (idempotent).
     * 
     * Acquires map_mutex to safely modify map structure.
     * Creates new Entry with default reader-writer lock state.
     * 
     * @param key Key to insert.
     * @param value Value to associate with key.
     * @return True if inserted (key was new), false if key already exists (no modification).
     * 
     * Thread-safe: Uses map_mutex to serialize inserts.
     */
    bool insert(const K& key, const V& value);

    /**
     * @brief ### Checks if a key exists in the map (read-only query).
     * 
     * Acquires map_mutex briefly to check existence.
     * Does NOT acquire entry's reader-writer lock (only checks map structure).
     * 
     * @param key Key to check.
     * @return True if key exists, false otherwise.
     * 
     * Note: Result may become stale immediately after return (entry could be deleted).
     * Prefer read() if you need the value (atomic check + read).
     */
    bool exists(const K& key) const;

    /**
     * @brief ### Reads the value associated with a key (returns a copy).
     * 
     * Acquires entry's read lock (allows concurrent reads, blocks if writer active).
     * Returns copy of value to allow safe access after lock release.
     * 
     * @param key Key to read.
     * @return std::optional containing value copy if key exists, std::nullopt if not found.
     * 
     * Thread-safe: Multiple readers can execute simultaneously on same entry.
     * Performance: O(1) average case (hash lookup + read lock acquisition).
     */
    std::optional<V> read(const K& key);

    /**
     * @brief ### Writes a new value to an existing key (replace operation).
     * 
     * Acquires entry's write lock (exclusive, blocks all readers and writers).
     * Does NOT create key if missing (returns false instead).
     * 
     * @param key Key to write (must already exist in map).
     * @param value New value to store (replaces old value).
     * @return True if key exists and was updated, false if key not found.
     * 
     * Thread-safe: Only one writer can access entry at a time.
     * Use case: Update client balance or last_processed_request_id.
     */
    bool write(const K& key, const V& value);

    /**
     * @brief ### Atomically performs an operation on two entries (transaction primitive).
     * 
     * Locks both entries for writing in fixed order to prevent deadlocks.
     * Callback receives references to both values for in-place modification.
     * 
     * Deadlock prevention:
     * - Orders locks by Entry pointer address (lower address locked first)
     * - Ensures consistent global locking order across all threads
     * - Prevents AB-BA deadlock (Thread 1: lock(A,B), Thread 2: lock(B,A))
     * 
     * Special case: If key1 == key2 (self-operation), acquires single write lock.
     * 
     * @param key1 First key (must exist in map).
     * @param key2 Second key (must exist in map).
     * @param fn Callback function receiving (V& value1, V& value2) for modification.
     *           Executed while both entries are locked (atomic transaction).
     * @return True if both keys exist and operation performed, false if either key missing.
     * 
     * Use case: Bank transfer (debit sender, credit receiver atomically).
     * Example: atomic_pair_operation(sender_ip, receiver_ip, [](auto& s, auto& r) {
     *              s.balance -= 100;
     *              r.balance += 100;
     *          });
     */
    bool atomic_pair_operation(const K& key1, const K& key2,
                               const std::function<void(V&, V&)>& fn);

private:
    /// Map of entries, each with independent reader-writer lock
    /// Key = client IP, Value = shared_ptr<Entry<ClientInfo>>
    std::unordered_map<K, std::shared_ptr<Entry<V>>> data;
    
    /// Protects map structure modifications (insert, find operations)
    /// NOT used for protecting individual entry values (entries have own locks)
    std::mutex map_mutex;

    /**
     * @brief Helper to safely retrieve Entry shared_ptr (internal use only).
     * 
     * Acquires map_mutex, looks up key, returns shared_ptr or nullptr.
     * Shared_ptr keeps Entry alive even if map_mutex is released.
     * 
     * @param key Key to look up.
     * @return shared_ptr to Entry if found, nullptr otherwise.
     */
    std::shared_ptr<Entry<V>> get_entry(const K& key) {
        std::lock_guard<std::mutex> lock(map_mutex);
        auto it = data.find(key);
        if (it == data.end()) return nullptr;
        return it->second;
    }
};

// ===== Entry implementations =====

template<typename V>
void Entry<V>::lock_read() {
    std::unique_lock<std::mutex> lock(mutex);
    
    // Wait while:
    // 1. A writer is active (writer_active = true), OR
    // 2. Writers are waiting (waiting_writers > 0, writer preference policy)
    // This prevents reader starvation of writers (writer preference)
    cv.wait(lock, [&]{ return !writer_active && waiting_writers == 0; });
    
    active_readers++;  // Increment reader count (multiple readers allowed)
}

template<typename V>
void Entry<V>::unlock_read() {
    std::unique_lock<std::mutex> lock(mutex);
    active_readers--;  // Decrement reader count
    
    // If this was the last reader, notify waiting writers
    // (Writers wait for active_readers == 0)
    if (active_readers == 0) {
        cv.notify_all();  // Wake all waiting threads (writers will compete)
    }
}

template<typename V>
void Entry<V>::lock_write() {
    std::unique_lock<std::mutex> lock(mutex);
    
    // Increment waiting_writers BEFORE waiting
    // This blocks new readers (writer preference policy)
    waiting_writers++;
    
    // Wait while:
    // 1. A writer is active (writer_active = true), OR
    // 2. Readers are active (active_readers > 0)
    // Only one writer can be active at a time (exclusive access)
    cv.wait(lock, [&]{ return !writer_active && active_readers == 0; });
    
    waiting_writers--;  // We're no longer waiting (about to become active)
    writer_active = true;  // Mark this thread as the active writer
}

template<typename V>
void Entry<V>::unlock_write() {
    std::unique_lock<std::mutex> lock(mutex);
    writer_active = false;  // Release exclusive write access
    
    // Notify all waiting threads (both readers and writers)
    // They will compete for next lock acquisition
    cv.notify_all();
}

// ===== LockedMap implementations =====

template<typename K, typename V>
bool LockedMap<K,V>::insert(const K& key, const V& value) {
    std::lock_guard<std::mutex> lock(map_mutex);  // Protect map structure modification
    
    // try_emplace: inserts only if key doesn't exist (atomic check + insert)
    // Returns pair<iterator, bool> where bool = true if inserted
    auto [it, inserted] = data.try_emplace(key, std::make_shared<Entry<V>>());
    
    if (inserted) {
        // New entry created, initialize its value
        it->second->value = value;
    }
    // If not inserted, key already exists (no modification, idempotent)
    
    return inserted;
}

template<typename K, typename V>
bool LockedMap<K,V>::exists(const K& key) const {
    std::lock_guard<std::mutex> lock(map_mutex);  // Protect map structure read
    return data.find(key) != data.end();
}

template<typename K, typename V>
std::optional<V> LockedMap<K,V>::read(const K& key) {
    // Get shared_ptr to entry (acquires map_mutex briefly)
    std::shared_ptr<Entry<V>> entry_ptr = get_entry(key);
    if (!entry_ptr) return std::nullopt;  // Key doesn't exist
    
    // Acquire read lock on entry (allows concurrent reads)
    // map_mutex is already released here (fine-grained locking)
    entry_ptr->lock_read();
    V value_copy = entry_ptr->value;  // Copy value while locked
    entry_ptr->unlock_read();
    
    return value_copy;  // Return copy (safe to use after unlock)
}

template<typename K, typename V>
bool LockedMap<K,V>::write(const K& key, const V& value) {
    // Get shared_ptr to entry (acquires map_mutex briefly)
    std::shared_ptr<Entry<V>> entry_ptr = get_entry(key);
    if (!entry_ptr) return false;  // Key doesn't exist
    
    // Acquire write lock on entry (exclusive access)
    // map_mutex is already released here (fine-grained locking)
    entry_ptr->lock_write();
    entry_ptr->value = value;  // Modify value while locked
    entry_ptr->unlock_write();
    
    return true;
}

template<typename K, typename V>
bool LockedMap<K,V>::atomic_pair_operation(const K& key1, const K& key2,
                                            const std::function<void(V&, V&)>& fn) {
    // Step 1: Get shared_ptrs for both entries (acquires map_mutex briefly)
    std::shared_ptr<Entry<V>> entry1, entry2;
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        auto it1 = data.find(key1);
        auto it2 = data.find(key2);
        
        // Both keys must exist for atomic operation
        if (it1 == data.end() || it2 == data.end())
            return false;
        
        entry1 = it1->second;
        entry2 = it2->second;
    }
    // map_mutex released here (fine-grained locking)

    // Step 2: Handle self-operation (same key for both parameters)
    // Example: transfer from account to itself (no-op, but valid)
    if (entry1.get() == entry2.get()) {
        Entry<V>* single = entry1.get();
        single->lock_write();
        fn(single->value, single->value);  // Callback receives same reference twice
        single->unlock_write();
        return true;
    }

    // Step 3: Order locks by pointer address to prevent deadlock
    // Ensures consistent global locking order across all threads
    // Example: Thread 1 locks (A, B), Thread 2 locks (B, A)
    //          Without ordering: potential AB-BA deadlock
    //          With ordering: both threads lock lower address first
    Entry<V>* first;
    Entry<V>* second;
    if (entry1.get() < entry2.get()) {
        first = entry1.get();
        second = entry2.get();
    } else {
        first = entry2.get();
        second = entry1.get();
    }

    // Step 4: Lock both entries for writing (in ordered sequence)
    first->lock_write();   // Acquire first lock
    second->lock_write();  // Acquire second lock (no deadlock possible)

    // Step 5: Execute callback with references to values
    // Callback can modify both values atomically (both locked)
    fn(entry1->value, entry2->value);

    // Step 6: Unlock in reverse order (not strictly necessary, but good practice)
    second->unlock_write();
    first->unlock_write();
    
    return true;
}
