#include <iostream>

class Tracer {
 public:
  Tracer() { std::cout << " Tracer Constructor" << std::endl; }
  ~Tracer() { std::cout << " Tracer Destructor" << std::endl; }
};

int main() {
  using namespace std;
  cout << "cpp test begin --> " << endl;
  Tracer t;

  cout << "creat tt" << endl;
  {
    Tracer tt;
  }
  cout << "delete tt" << endl;

  cout << "cpp test end --> " << endl;
  return 0;
}