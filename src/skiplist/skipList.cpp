#include "../../include/skiplist/skiplist.h"
#include <cstdint>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace toni_lsm {

// ************************ SkipListIterator ************************
BaseIterator &SkipListIterator::operator++() {
  // TODO: Lab1.2 任务：实现SkipListIterator的++操作符
  // 迭代器的主要目的是按顺序遍历所有元素
  // 在跳表中，第0层已经包含了所有节点，并且是有序的
  // 使用 forward_[0] 可以保证按顺序访问所有元素，不会遗漏任何节点
  if (current) {
    current = current->forward_[0];
  }
  return *this;
}

bool SkipListIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的==操作符
  // 比较两个迭代器类型是否相同
  if (other.get_type() != IteratorType::SkipListIterator) {
    return false;
  }
  // 这里使用 static cast 和 dynamic cast 的区别是？ static cast 遇到派生类有基类不包含的信息时，是不安全的
  // 因此这里使用 dynamic cast 是更好的选择，能安全的向下转换
  const SkipListIterator &other_iter = dynamic_cast<const SkipListIterator &>(other);
  return current == other_iter.current;
}

bool SkipListIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab1.2 任务：实现SkipListIterator的!=操作符
  return !(*this == other);
}

SkipListIterator::value_type SkipListIterator::operator*() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的*操作符
  if (!current) {
    throw std::runtime_error("Dereference an invalid iterator");
  }
  return {current->key_, current->value_};
}

IteratorType SkipListIterator::get_type() const {
  // TODO: Lab1.2 任务：实现SkipListIterator的get_type
  // ? 主要是为了熟悉基类的定义和继承关系
  return IteratorType::SkipListIterator;
}

bool SkipListIterator::is_valid() const {
  return current && !current->key_.empty();
}
bool SkipListIterator::is_end() const { return current == nullptr; }

std::string SkipListIterator::get_key() const { return current->key_; }
std::string SkipListIterator::get_value() const { return current->value_; }
uint64_t SkipListIterator::get_tranc_id() const { return current->tranc_id_; }

// ************************ SkipList ************************
// 构造函数
SkipList::SkipList(int max_lvl) : max_level(max_lvl), current_level(1) {
  // head 头结点用于遍历跳表，kv为空，当前层数为max_level，事务id为0
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  dis_01 = std::uniform_int_distribution<>(0, 1);
  dis_level = std::uniform_int_distribution<>(0, (1 << max_lvl) - 1);
  gen = std::mt19937(std::random_device()());
}

int SkipList::random_level() {
  // ? 通过"抛硬币"的方式随机生成层数：
  // ? - 每次有50%的概率增加一层
  // ? - 确保层数分布为：第1层100%，第2层50%，第3层25%，以此类推
  // ? - 层数范围限制在[1, max_level]之间，避免浪费内存
  // TODO: Lab1.1 任务：插入时随机为这一次操作确定其最高连接的链表层数
  int level = 1;
  while (dis_01(gen) && level < max_level) {
    level++;
  }
  return level;
}

// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {})", key, value, tranc_id);

  // TODO: Lab1.1  任务：实现插入或更新键值对
  int new_level = std::max(random_level(), current_level);
  // 记录每一层需要更新的节点是哪个
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, nullptr);

  auto new_node = std::make_shared<SkipListNode>(key, value, new_level, tranc_id);
  auto cur = head;
  // 从最高层开始, 每次检索从最大高度出发，直到来到首层，找到新节点插入的位置
  for (int i = current_level - 1; i >= 0; i--) {
    // forward 是指向不同层级的下一个节点的指针数组
    // cur 是当前节点，cur->forward_[i] 是当前节点在第 i 层的下一个节点
    // 这个 while 循环：当前层移动，停留最终在第i层需要更新的位置；只要下一个节点的 key 小于待插入 key，就一直往右走
    // 当这一层找到第一个 >= key 的节点时，结束这一层的遍历，转到下一层 i-1，从这个「当前节点」继续向下找
    while (cur->forward_[i] && *cur->forward_[i] < *new_node) {
      cur = cur->forward_[i];
    }
    spdlog::trace("SkipList--put({}, {}, {}), level{} needs updating", key,
                  value, tranc_id, i);
    update[i] = cur;
  }

  // 定位到层 0 上可能相等的节点
  cur = cur->forward_[0];

  if (cur && cur->key_ == key && cur->tranc_id_ == tranc_id) {
    // 如果当前节点存在，并且 key 和 tranc_id 都相同，则更新 value
    size_bytes += value.size() - cur->value_.size();
    cur->value_ = value;
    cur->tranc_id_ = tranc_id;
    spdlog::trace("SkipList--put({}, {}, {}), key and tranc_id_ is the same, "
                  "only update value to {}",
                  key, value, tranc_id, value);
    return;
  }

  // 对于当前最大层数到新节点最大层数之间的层，需要把新节点插入到 head 节点之后，因此 update[i] = head
  if (new_level > current_level) {
    for (int i = current_level; i < new_level; i++) {
      update[i] = head;
    }
    current_level = new_level;
  }

  // 生成随机数，决定是否在每一层更新节点
  int random_num = dis_level(gen);
  size_bytes += new_node->key_.size() + new_node->value_.size() + sizeof(uint64_t);
  for (int i = 0; i < new_level; i++) {
    bool need_update = (i == 0) || (new_level > current_level) || (random_num & (1 << i));
    if (need_update) {
      // 插入新节点，即在 update[i] 和 cur 之间插入新节点
      new_node->forward_[i] = update[i]->forward_[i]; // 可能为 nullptr
      if (new_node->forward_[i]) {
        // 如果下一层节点存在，则更新下一层节点的 backward 指针，用 weak_ptr 避免循环引用
        new_node->forward_[i]->set_backward(i, new_node);
      }
      update[i]->forward_[i] = new_node;
      new_node->backward_[i] = update[i];
    } else {
      // 如果不需要更新，则当前层之后更高层都不更新了
      break;
    }
  }
  
  current_level = std::max(current_level, new_level);
  // ? Hint: 你需要保证不同`Level`的步长从底层到高层逐渐增加
  // ? 你可能需要使用到`random_level`函数以确定层数, 其注释中为你提供一种思路
  // ? tranc_id 为事务id, 现在你不需要关注它, 直接将其传递到 SkipListNode 的构造函数中即可
}

// 查找键值对
SkipListIterator SkipList::get(const std::string &key, uint64_t tranc_id) {
  // spdlog::trace("SkipList--get({}) called", key);
  // ? 你可以参照上面的注释完成日志输出以便于调试
  // ? 日志为输出到你执行二进制所在目录下的log文件夹

  // TODO: Lab1.1 任务：实现查找键值对,
  auto cur = head;
  // 从最高层开始, 每次检索从最大高度出发，直到来到首层
  for (int i = current_level - 1; i >= 0; i--) {
    while (cur->forward_[i] && cur->forward_[i]->key_ < key) {
      cur = cur->forward_[i];
    }
  }

  // 来到首层，找到第一个 >= key 的节点
  cur = cur->forward_[0];
  if (tranc_id == 0) {
    if (cur && cur->key_ == key) {
      return SkipListIterator(cur);
    }
  } else if (tranc_id != 0) {
    while (cur && cur->key_ == key) {
      if (cur->tranc_id_ == tranc_id) {
        return SkipListIterator(cur);
      } else {
        cur = cur->forward_[0];
      }
    }
  }
  // TODO: 并且你后续需要额外实现SkipListIterator中的TODO部分(Lab1.2)
  spdlog::trace("SkipList--get({}): not found", key);
  return SkipListIterator{};
}

// 删除键值对
// ! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除,
// ! 这里只是为了实现完整的 SkipList 不会真正被上层调用
void SkipList::remove(const std::string &key) {
  // TODO: Lab1.1 任务：实现删除键值对
  auto cur = head;
  std::vector<std::shared_ptr<SkipListNode>> update(current_level, nullptr);
  for (int i = current_level - 1; i >= 0; i--) {
    while (cur->forward_[i] && cur->forward_[i]->key_ < key) {
      cur = cur->forward_[i];
    }
    update[i] = cur;
  }

  // 来到首层
  cur = cur->forward_[0];

  // 如果当前节点存在，并且 key 相同，则删除该节点
  if (cur && cur->key_ == key) {
    for (int i = 0; i < current_level; i++) {
      if (update[i]->forward_[i] == cur) {
        update[i]->forward_[i] = cur->forward_[i];
        if (cur->forward_[i]) {
          cur->forward_[i]->set_backward(i, update[i]);
        }
      } else {
        break;
      }
    }
    size_bytes -= cur->key_.size() + cur->value_.size() + sizeof(uint64_t);

    // 如果删除节点后最高层为空，则更新最高层
    while (current_level > 1 && !head->forward_[current_level - 1]) {
      current_level--;
    }
  }
}

// 刷盘时可以直接遍历最底层链表
std::vector<std::tuple<std::string, std::string, uint64_t>> SkipList::flush() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  spdlog::debug("SkipList--flush(): Starting to flush skiplist data");

  std::vector<std::tuple<std::string, std::string, uint64_t>> data;
  auto node = head->forward_[0];
  while (node) {
    data.emplace_back(node->key_, node->value_, node->tranc_id_);
    node = node->forward_[0];
  }

  spdlog::debug("SkipList--flush(): Flushed {} entries", data.size());

  return data;
}

size_t SkipList::get_size() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  return size_bytes;
}

// 清空跳表，释放内存
void SkipList::clear() {
  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  size_bytes = 0;
}

SkipListIterator SkipList::begin() {
  // 返回第0层的首个节点
  // 这里直接使用 head->forward_[0] 作为初始节点，确保从第 0层开始
  // 这样 iter 的 current 指针始终保持在第0层
  // return SkipListIterator(head->forward[0], rw_mutex);
  return SkipListIterator(head->forward_[0]);
}

SkipListIterator SkipList::end() {
  return SkipListIterator(); // 使用空构造函数
}

// 找到前缀的起始位置
// 返回第一个前缀匹配或者大于前缀的迭代器
SkipListIterator SkipList::begin_preffix(const std::string &preffix) {
  // TODO: Lab1.3 任务：实现前缀查询的起始位置
  auto cur = head;
  // 从最高层开始，找到第一个 >= preffix 的节点
  for (int i = current_level - 1; i >= 0; i--) {
    while (cur->forward_[i] && cur->forward_[i]->key_ < preffix) {
      cur = cur->forward_[i];
    }
  }
  cur = cur->forward_[0];
  if (cur && cur->key_ == preffix) {
    spdlog::trace("SkipList--begin_preffix({}): found exact match {}", preffix, cur->key_);
  }
  return SkipListIterator(cur);
}

// 找到前缀的终结位置
SkipListIterator SkipList::end_preffix(const std::string &prefix) {
  // TODO: Lab1.3 任务：实现前缀查询的终结位置

  auto cur = head;
  for (int i = current_level - 1; i >= 0; i--) {
    while (cur->forward_[i] && cur->forward_[i]->key_ < prefix) {
      cur = cur->forward_[i];
    }
  }
  cur = cur->forward_[0];

  while (cur && cur->key_.substr(0, prefix.size()) == prefix) {
    cur = cur->forward_[0];
  }
  if (cur) {
    spdlog::trace("SkipList--end_preffix({}): found exact match {}", prefix, cur->key_);
  } else {
    spdlog::trace("SkipList--end_preffix({}): end at the end of the skiplist", prefix);
  }
  return SkipListIterator(cur);
}

// ? 这里单调谓词的含义是, 整个数据库只会有一段连续区间满足此谓词
// ? 例如之前特化的前缀查询，以及后续可能的范围查询，都可以转化为谓词查询
// ? 返回第一个满足谓词的位置和最后一个满足谓词的迭代器
// ? 如果不存在, 范围nullptr
// ? 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// ? predicate返回值:
// ?   0: 满足谓词
// ?   >0: 不满足谓词, 需要向右移动
// ?   <0: 不满足谓词, 需要向左移动
// ! Skiplist 中的谓词查询不会进行事务id的判断, 需要上层自己进行判断
std::optional<std::pair<SkipListIterator, SkipListIterator>>
SkipList::iters_monotony_predicate(
    std::function<int(const std::string &)> predicate) {
  // TODO: Lab1.3 任务：实现谓词查询的起始位置
  auto cur = head;
  SkipListIterator start_iter(nullptr);
  SkipListIterator end_iter(nullptr);
  // 从最高层开始，找到第一个满足谓词的节点
  bool find_start = false;
  for (int i = current_level - 1; i >= 0; i--) {
    while (!find_start) {
      auto forward_i = cur->forward_[i];
      if (forward_i == nullptr) {
        break;
      }
      auto direction = predicate(forward_i->key_);
      if (direction == 0) {
        find_start = true;
        cur = forward_i;
        break;
      } else if (direction > 0) {
        // 下一个位置不满足谓词，但方向是正确的（需要向右移动）
        cur = forward_i;
      } else {
        // 下一个位置不满足谓词，且方向是错误的（没法向左移动， 需要减小步长/层数）
        break;
      }
    }
  }
  if (!find_start) {
    spdlog::trace("SkipList--iters_monotony_predicate(): not found");
    return std::nullopt;
  }
  // 把当前的cur保存到end_cur，便于后续使用
  auto end_cur = cur;

  // cur 已经满足谓词，但有可能不是第一个满足谓词的节点，需要前向遍历找到第一个满足谓词的节点
  // 注意这里 cur 是满足谓词的节点，所以需要从 cur 的层数开始，而不是从 current_level 开始
  for (int i = cur->backward_.size() - 1; i >= 0; i--) {
    while (true) {
      // weak_ptr 的 lock 方法返回一个 shared_ptr，如果 shared_ptr 为空，则返回 nullptr
      if (cur->backward_[i].lock() == nullptr || 
          cur->backward_[i].lock() == head) {
        // 当前层没有前向节点，或者前向节点是头结点
        break;
      }
      auto direction = predicate(cur->backward_[i].lock()->key_);
      if (direction == 0) {
        cur = cur->backward_[i].lock();
        continue;
      } else if (direction > 0) {
        // 前一个位置不满足谓词，需要尝试更小的步长
        break;
      } else {
        // 因为当前位置满足了谓词，因此前一个位置只能是>=0
        // 出现这种情况属于跳表实现错误，需要排查
        spdlog::error("iter_predicate: invalid direction");
        throw std::runtime_error("iter_predicate: invalid direction");
      }
    }
  }
  start_iter = SkipListIterator(cur);

  // 从 end_cur 开始，向后遍历找到最后一个满足谓词的节点
  for (int i = end_cur->forward_.size() - 1; i >= 0; i--) {
    while (true) {
      if (end_cur->forward_[i] == nullptr) {
        break;
      }
      auto direction = predicate(end_cur->forward_[i]->key_);
      if (direction == 0) {
        end_cur = end_cur->forward_[i];
        continue;
      } else if (direction < 0) {
        // 下一个位置不满足谓词，需要尝试更小的步长
        break;
      } else {
        // 因为当前位置满足了谓词，因此后一个位置只能是 <=0
        // 出现这种情况属于跳表实现错误，需要排查
        spdlog::error("iter_predicate: invalid direction");
        throw std::runtime_error("iter_predicate: invalid direction");
      }
    }
  }
  end_iter = SkipListIterator(end_cur);
  ++end_iter;
  spdlog::trace("SkipList--iters_monotony_predicate(): range found");

  return std::make_optional<std::pair<SkipListIterator, SkipListIterator>>(
      std::make_pair(start_iter, end_iter));
}

// ? 打印跳表, 你可以在出错时调用此函数进行调试
void SkipList::print_skiplist() {
  for (int level = 0; level < current_level; level++) {
    std::cout << "Level " << level << ": ";
    auto current = head->forward_[level];
    while (current) {
      std::cout << current->key_;
      current = current->forward_[level];
      if (current) {
        std::cout << " -> ";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
} // namespace toni_lsm