#include <cstring>
#include <iostream>

class String {
 public:
  String() {
    data = new char[1];
    data[0] = '\0';
  }

  String(const char* s) {
    data = new char[strlen(s) + 1];
    strcpy(data, s);
  }

  ~String() { delete[] data; }

  String(const String& other) {
    data = new char[strlen(other.c_str()) + 1];
    strcpy(data, other.c_str());
    // 浅拷贝
    // data = other.data;
  }

  String& operator=(const String& other) {
    if (this == &other) return *this;

    char* new_data = new char[strlen(other.c_str()) + 1];
    strcpy(new_data, other.c_str());

    delete[] data;
    data = new_data;

    return *this;
  }

  const char* c_str() const { return data; }

 private:
  char* data;
};

int main() {
  String s1("hello");
  String s2 = s1;

  String s3;
  s3 = s1;

  s3 = s3;

  std::cout << "s1 = " << s1.c_str() << std::endl;
  std::cout << "s2 = " << s2.c_str() << std::endl;
  std::cout << "s3 = " << s3.c_str() << std::endl;

  return 0;
}