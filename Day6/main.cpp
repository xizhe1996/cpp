#include <iostream>

class Resource {
 public:
  Resource(const char* name) : _name(name) {
    std::cout << "require" << _name << std::endl;
  }

  // 允许触发异常 默认是不允许触发异常
  ~Resource() noexcept(false) {
    std::cout << "release" << _name << std::endl;
    throw std::runtime_error(" error in destructor");
  }

 private:
  const char* _name;
};

//
void funcA() {
  Resource a("A");
  throw std::runtime_error(" error in funcA");
}

void funcB() {
  Resource b1("B1");
  Resource b2("B2");
  throw std::runtime_error(" error in funcB");
}

void funcC() {
  try {
    funcB();
  } catch (const std::exception& e) {
    std::cout << "error in funcC, " << e.what() << std::endl;
  }
}

int main() {
  using namespace std;
  try {
    Resource A{"a"};
  } catch (const exception& e) {
    std::cout << e.what() << std::endl;
  }
#if 0
  try {
    funcA();
  } catch (...) {
    std::cout << "caugh in main" << std::endl;
  }

  std::cout << "=================" << std::endl;

  funcC();
#endif
  return 0;
}