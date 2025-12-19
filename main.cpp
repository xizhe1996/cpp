#include <iostream>

class Tracer {
 public:
  int id;
  // constructor
  Tracer(int id) : id(id) { std::cout << "ctor id: " << id << std::endl; }

#if 1
  // copy constructor
  Tracer(const Tracer& other) : id(other.id) {
    std::cout << "copy ctor id: " << id << std::endl;
  }
#endif

  // destructor
  ~Tracer() { std::cout << " dtor id:" << id << std::endl; }
};

void process(Tracer t) {
  using namespace std;
  cout << "inside process, id:" << t.id << endl;
}

int main() {
  using namespace std;
  Tracer a(1);
  cout << "should be call constructor." << endl;
  process(a);
  return 0;
}