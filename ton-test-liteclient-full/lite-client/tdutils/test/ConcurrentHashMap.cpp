#include <cstdio>
#include "td/utils/tests.h"
#include "td/utils/benchmark.h"
#include "td/utils/SpinLock.h"
#include "td/utils/HazardPointers.h"
#include "td/utils/ConcurrentHashTable.h"
#include <algorithm>

#if TD_WITH_ABSEIL
#include <absl/container/flat_hash_map.h>
#else
#include <unordered_map>
#endif

#if TD_WITH_JUNCTION
#include <third-party/libcuckoo/libcuckoo/cuckoohash_map.hh>
#endif

#if TD_WITH_JUNCTION
#include <junction/ConcurrentMap_Grampa.h>
#include <junction/ConcurrentMap_Linear.h>
#include <junction/ConcurrentMap_Leapfrog.h>
#endif
namespace td {

// Non resizable HashMap. Just an example
template <class KeyT, class ValueT>
class ArrayHashMap {
 public:
  ArrayHashMap(size_t n) : array_(n) {
  }
  struct Node {
    std::atomic<KeyT> key{KeyT{}};
    std::atomic<ValueT> value{};
  };
  static std::string get_name() {
    return "ArrayHashMap";
  }
  KeyT empty_key() const {
    return KeyT{};
  }

  void insert(KeyT key, ValueT value) {
    array_.with_value(key, true, [&](auto &node_value) { node_value.store(value, std::memory_order_release); });
  }
  ValueT find(KeyT key, ValueT value) {
    array_.with_value(key, false, [&](auto &node_value) { value = node_value.load(std::memory_order_acquire); });
    return value;
  }

 private:
  AtomicHashArray<KeyT, std::atomic<ValueT>> array_;
};

template <class KeyT, class ValueT>
class ConcurrentHashMapMutex {
 public:
  ConcurrentHashMapMutex(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapMutex";
  }
  void insert(KeyT key, ValueT value) {
    std::unique_lock<std::mutex> lock(mutex_);
    hash_map_.emplace(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = hash_map_.find(key);
    if (it == hash_map_.end()) {
      return default_value;
    }
    return it->second;
  }

 private:
  std::mutex mutex_;
#if TD_WITH_ABSEIL
  absl::flat_hash_map<KeyT, ValueT> hash_map_;
#else
  std::unordered_map<KeyT, ValueT> hash_map_;
#endif
};
template <class KeyT, class ValueT>
class ConcurrentHashMapSpinlock {
 public:
  ConcurrentHashMapSpinlock(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapSpinlock";
  }
  void insert(KeyT key, ValueT value) {
    auto guard = spinlock_.lock();
    hash_map_.emplace(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    auto guard = spinlock_.lock();
    auto it = hash_map_.find(key);
    if (it == hash_map_.end()) {
      return default_value;
    }
    return it->second;
  }

 private:
  td::SpinLock spinlock_;
#if TD_WITH_ABSEIL
  absl::flat_hash_map<KeyT, ValueT> hash_map_;
#else
  std::unordered_map<KeyT, ValueT> hash_map_;
#endif
};
#if TD_WITH_LIBCUCKOO
template <class KeyT, class ValueT>
class ConcurrentHashMapLibcuckoo {
 public:
  ConcurrentHashMapLibcuckoo(size_t) {
  }
  static std::string get_name() {
    return "ConcurrentHashMapLibcuckoo";
  }
  void insert(KeyT key, ValueT value) {
    hash_map_.insert(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    hash_map_.find(key, default_value);
    return default_value;
  }

 private:
  cuckoohash_map<KeyT, ValueT> hash_map_;
};
#endif
#if TD_WITH_JUNCTION
template <class KeyT, class ValueT>
class ConcurrentHashMapJunction {
 public:
  ConcurrentHashMapJunction(size_t size) : hash_map_() {
  }
  static std::string get_name() {
    return "ConcurrentHashMapJunction";
  }
  void insert(KeyT key, ValueT value) {
    hash_map_.assign(key, value);
  }
  ValueT find(KeyT key, ValueT default_value) {
    return hash_map_.get(key);
  }
  ~ConcurrentHashMapJunction() {
    junction::DefaultQSBR.flush();
  }

 private:
  junction::ConcurrentMap_Leapfrog<KeyT, ValueT> hash_map_;
};
#endif
}  // namespace td

template <class HashMap>
class HashMapBenchmark : public td::Benchmark {
  struct Query {
    int key;
    int value;
  };
  std::vector<Query> queries;
  std::unique_ptr<HashMap> hash_map;

  size_t threads_n = 16;
  int mod_;
  constexpr static size_t mul_ = 7273;  //1000000000 + 7;
  int n_;

 public:
  HashMapBenchmark(size_t threads_n) : threads_n(threads_n) {
  }
  std::string get_description() const override {
    return hash_map->get_name();
  }
  void start_up_n(int n) override {
    n *= (int)threads_n;
    n_ = n;
    hash_map = std::make_unique<HashMap>(n * 2);
  }

  void run(int n) override {
    n = n_;
    std::vector<td::thread> threads;

    for (size_t i = 0; i < threads_n; i++) {
      size_t l = n * i / threads_n;
      size_t r = n * (i + 1) / threads_n;
      threads.emplace_back([l, r, this] {
        for (size_t i = l; i < r; i++) {
          auto x = int((i + 1) * mul_ % n_) + 3;
          auto y = int(i + 2);
          hash_map->insert(x, y);
        }
      });
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }

  void tear_down() override {
    for (int i = 0; i < n_; i++) {
      auto x = int((i + 1) * mul_ % n_) + 3;
      auto y = int(i + 2);
      ASSERT_EQ(y, hash_map->find(x, -1));
    }
    queries.clear();
    hash_map.reset();
  }

 private:
};

template <class HashMap>
void bench_hash_map() {
  td::bench(HashMapBenchmark<HashMap>(16));
  td::bench(HashMapBenchmark<HashMap>(1));
}

TEST(ConcurrentHashMap, Benchmark) {
  bench_hash_map<td::ConcurrentHashMap<int, int>>();
  bench_hash_map<td::ArrayHashMap<int, int>>();
  bench_hash_map<td::ConcurrentHashMapSpinlock<int, int>>();
  bench_hash_map<td::ConcurrentHashMapMutex<int, int>>();
#if TD_WITH_LIBCUCKOO
  bench_hash_map<td::ConcurrentHashMapLibcuckoo<int, int>>();
#endif
#if TD_WITH_JUNCTION
  bench_hash_map<td::ConcurrentHashMapJunction<int, int>>();
#endif
}

