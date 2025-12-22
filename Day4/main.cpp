#include <iostream>
#include <memory>

class Tracer {
  std::unique_ptr<int> p;
  // std::shared_ptr<int> p;
};

int main() {
  using namespace std;
  Tracer t;
  // 编译报错 因为unique_ptr不支持拷贝(delete 拷贝构造函数实现?)
  // Tracer a = t;
  // 使用移动构造 编译成功
  Tracer a = std::move(t);
  return 0;
}