#include <iostream>

class Tracer {
 public:
  int id;
  // constructor
  Tracer(int id) : id(id) { std::cout << "ctor id: " << id << std::endl; }

  // 显示的禁止拷贝构造
  // Tracer(const Tracer&) = delete;

  // 编译器会生成默认拷贝构造

#if 0
  // copy constructor
  Tracer(const Tracer& other) : id(other.id) {
    std::cout << "copy ctor id: " << id << std::endl;
  }
#endif

#if 0
  // move constructor
  Tracer(Tracer&& other) : id(other.id) {
    std::cout << "move ctor id: " << id << std::endl;
    other.id = -1;
  }
#endif

  // destructor
  ~Tracer() { std::cout << "dtor id:" << id << std::endl; }
};

void process(Tracer t) {
  using namespace std;
  cout << "inside process, id:" << t.id << endl;
}

Tracer make() {
  Tracer t(1);
  std::cout << "make ..." << std::endl;
  return t;
}

int main() {
  using namespace std;
  Tracer a = make();
  cout << "a.id: " << a.id << endl;
  return 0;
}