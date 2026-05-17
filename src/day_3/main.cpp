#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <vector>

#if 0
int main() {
  std::vector<int> v{1, 3, 4, 5, 2};
  std::sort(v.begin(), v.end(), [](int x, int y) { return x > y; });
  std::for_each(v.begin(), v.end(), [](int x) { std::cout << x << " "; });

  std::cout << std::endl;

  std::vector<std::pair<std::string, int>> scores{
      {"a", 100}, {"b", 80}, {"c", 90}, {"d", 80}};

  std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  });

  std::for_each(scores.begin(), scores.end(), [](const auto& a) {
    std::cout << a.first << ":" << a.second << std::endl;
  });

  std::vector<int> vv = {1, 3, 5, 8, 9};

  auto it =
      std::find_if(vv.begin(), vv.end(), [](int x) { return x % 2 == 0; });

  if (it != vv.end()) std::cout << *it << std::endl;

  return 0;
}

#endif

int main() {
  std::string s = "hello";
  std::unordered_map<char, int> mp;

  for (auto c : s) {
    mp[c]++;
  }

  for (auto& kv : mp) {
    std::cout << kv.first << ":" << kv.second << std::endl;
  }

  // find

  std::unordered_map<std::string, int> scores;
  scores["alice"] = 90;
  scores["bob"] = 80;
  auto it = scores.find("tom");

  if (it != scores.end())
    std::cout << it->first << ":" << it->second << std::endl;

  if (scores.count("tom") == 0) std::cout << "not find tom" << std::endl;

  std::cout << "size:" << scores.size() << std::endl;

  std::cout << scores["tom"] << std::endl;
  std::cout << "size:" << scores.size() << std::endl;

  return 0;
}