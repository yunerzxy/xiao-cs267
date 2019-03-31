#pragma once

#include <upcxx/upcxx.hpp>
#include "kmer_t.hpp"

struct HashMap {
  //std::vector <kmer_pair> data;
  //std::vector <int> used;
  std::vector<upcxx::global_ptr<kmer_pair>> data;
  std::vector<upcxx::global_ptr<int>> used;

  size_t my_size;
  size_t global_size;
  int n_proc;

  size_t size() const noexcept;

  HashMap(size_t size);

  // Most important functions: insert and retrieve
  // k-mers from the hash table.
  bool insert(const kmer_pair &kmer, upcxx::atomic_domain<int>& ad);
  bool find(const pkmer_t &key_kmer, kmer_pair &val_kmer);

  // Helper functions

  // Write and read to a logical data slot in the table.
  void write_slot(uint64_t slot, const kmer_pair &kmer);
  kmer_pair read_slot(uint64_t slot);

  // Request a slot or check if it's already used.
  bool request_slot(uint64_t slot, upcxx::atomic_domain<int>& ad);
  bool slot_used(uint64_t slot);
};

HashMap::HashMap(size_t size) {
  nprocs = upcxx::rank_n();
  //my_rank = upcxx::rank_me();
  my_size = size_t ((size + nprocs -1 )/ nprocs);
  global_size = size;

  data.resize(nprocs);
  used.resize(nprocs);
  data[upcxx::rank_me()] = upcxx::new_array<kmer_pair>(my_size);
  used[upcxx::rank_me()] = upcxx::new_array<int>(my_size);
  for (int i=0; i<nprocs; i++){
    data[i] = upcxx::broadcast(data[i],i).wait();
    used[i] = upcxx::broadcast(used[i],i).wait();
  }
}

bool HashMap::insert(const kmer_pair &kmer, upcxx::atomic_domain<int>& ad) {
  uint64_t hash = kmer.hash();
  uint64_t probe = 0;
  //uint64_t p = 1;
  bool success = false;
  do {
    uint64_t slot = (hash + probe++) % global_size;
    //uint64_t slot = (hash + probe) % global_size;
    //probe = p * p++; // quadratic probing
    success = request_slot(slot, ad);
    if (success) {
      write_slot(slot, kmer);
    }
  } while (!success && probe < global_size);
  return success;
}

bool HashMap::find(const pkmer_t &key, kmer_pair &val) {
  uint64_t hash = key.hash();
  uint64_t probe = 0;
  //uint64_t p = 1;
  bool success = false;
  do {
    uint64_t slot = (hash + probe++) % global_size;
    //probe = p * p++;
    if (slot_used(slot)) {
      val = read_slot(slot);
      if (val.kmer == key) {
        success = true;
      }
    }
  } while (!success && probe < global_size);
  if (!success){
    uint64_t slot = (hash + probe) % global_size;
    printf("global_size: %d, probe: %d\n", global_size, probe);
    printf("global slot: %d, slot rank: %d, local slot: %d\n", slot, slot / size(), slot % my_size);
  }
  return success;
}

bool HashMap::slot_used(uint64_t slot) {
  upcxx::future<int> l_used = upcxx::rget(used[floor(slot / size())] + slot % size());
  l_used.wait();
  return l_used.result() == 1;
}

void HashMap::write_slot(uint64_t slot, const kmer_pair &kmer) {
  if (slot >= size() || slot < 0) return;
  upcxx::rput(kmer, data[floor(slot / size())] + slot % size()).wait();
}

kmer_pair HashMap::read_slot(uint64_t slot) {
  // if (slot >= global_size || slot < 0) 
  //   throw std::runtime_error("out of scope");
  return upcxx::rget(data[floor(slot / size())] + slot % size()).wait();
}

bool HashMap::request_slot(uint64_t slot, upcxx::atomic_domain<int>& ad) {
  upcxx::future <int> l_used = ad.fetch_add(used[floor(slot / size())] + slot % size(), 1, std::memory_order_relaxed);
  l_used.wait();
  return l_used.result() == 0;
}

size_t HashMap::size() const noexcept {
  return my_size;
}
