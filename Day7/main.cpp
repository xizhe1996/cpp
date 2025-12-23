#include <iostream>
#include <vector>

class Buffer {
 public:
  explicit Buffer(size_t size) : _size(size), data(new int[size]) {
    std::cout << "alloc" << size << std::endl;
  }

  Buffer(const Buffer& b) : _size(b._size), data(new int[_size]) {
    std::cout << "copy constructor" << _size << std::endl;
  }

  Buffer(Buffer&& b) noexcept : _size(b._size), data(b.data) {
    b.data = nullptr;
    std::cout << "move constructor" << _size << std::endl;
  }

  ~Buffer() {
    delete[] data;
    std::cout << "free" << _size << std::endl;
  }

 private:
  size_t _size;
  int* data;
};

class Widget {
 public:
  void add(size_t n) { _widget.push_back(Buffer(n)); }

  void add_strong(size_t n) {
    auto tmp = _widget;
    tmp.emplace_back(n);
    _widget.swap(tmp);
  }

 private:
  std::vector<Buffer> _widget;
};

int main() {
  std::vector<Buffer> v;
  v.reserve(1);

  std::cout << "push 1" << std::endl;
  v.emplace_back(10);

  std::cout << "push 2" << std::endl;
  v.emplace_back(20);

  std::cout << "test over" << std::endl;
  return 0;
}