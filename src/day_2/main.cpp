#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

class Connection {
 public:
  Connection(int id) {
    id_ = id;
    std::cout << "Connection " << id_ << " created\n";
  }

  ~Connection() { std::cout << "Connection " << id_ << " destroyed\n"; }

  void send(const std::string& msg) {
    std::cout << "Connection " << id_ << " send: " << msg << std::endl;
  }

 private:
  int id_;
};

void useConnection(Connection& conn) { conn.send("borrow use"); }

void takeConnection(std::unique_ptr<Connection> conn) {
  conn->send("take ownership");
}

std::vector<std::shared_ptr<Connection>> g_connections;
void saveConnection(std::shared_ptr<Connection> conn) {
  g_connections.push_back(conn);
}

// ptr
#if 0
int main() {
  auto up = std::make_unique<Connection>(1);
  useConnection(*up);
  takeConnection(std::move(up));

  if (!up) std::cout << "up is NULL\n";

  auto sp = std::make_shared<Connection>(2);
  std::cout << "sp use_count: " << sp.use_count() << std::endl;

  saveConnection(sp);
  std::cout << "sp use_count: " << sp.use_count() << std::endl;

  std::weak_ptr<Connection> wp = sp;

  sp.reset();
  std::cout << "sp use_count: " << sp.use_count() << std::endl;
  if (!wp.expired()) std::cout << "not expired\n";
  std::cout << sizeof(wp) << std::endl;

  g_connections.clear();
  std::cout << "sp use_count: " << sp.use_count() << std::endl;
  if (wp.expired()) std::cout << "expired\n";

  return 0;
}

void print(const std::vector<int>& v, const std::string& msg) {
  std::cout << msg << " size=" << v.size() << " capacity=" << v.capacity()
            << " data=" << static_cast<const void*>(v.data()) << std::endl;
}

int main() {
  std::vector<int> v;
  print(v, "initial");

  for (int i = 0; i < 10; ++i) {
    v.push_back(i);
    print(v, "after push_back");
  }

  return 0;
}

#endif

int main() {
  std::vector<int> v{1, 2, 3, 4, 5, 6};

#if 0
  for (auto it = v.begin(); it != v.end();) {
    if (*it % 2 == 0)
      it = v.erase(it);
    else
      ++it;
  }
#endif

  v.erase(std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; }),
          v.end());
  for (auto n : v) std::cout << n << " ";
  return 0;
}