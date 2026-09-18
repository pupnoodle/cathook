#ifndef UTL_VECTOR_HPP
#define UTL_VECTOR_HPP

template <typename T>
class CUtlVector {
public:
  struct CUtlMemory {
    T* memory = nullptr;
    int allocation_count = 0;
    int grow_size = 0;
  };

  CUtlVector() = default;

  CUtlVector(T* external_memory, int external_count) {
    Reset(external_memory, external_count);
  }

  int Count() const {
    return size;
  }

  T* Base() {
    return memory.memory;
  }

  const T* Base() const {
    return memory.memory;
  }

  T& operator[](int index) {
    return memory.memory[index];
  }

  const T& operator[](int index) const {
    return memory.memory[index];
  }

  void Reset(T* external_memory, int external_count) {
    memory.memory = external_memory;
    memory.allocation_count = external_count;
    memory.grow_size = -1;
    size = 0;
    elements = external_memory;
  }

  CUtlMemory memory{};
  int size = 0;
  T* elements = nullptr;
};

#endif
