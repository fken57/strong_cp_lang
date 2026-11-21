#ifndef FKEN_RUNTIME_HPP
#define FKEN_RUNTIME_HPP

#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <functional>
#include <limits>
#include <queue>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fken {

class Int {
    using Big = boost::multiprecision::cpp_int;
    std::variant<std::int64_t, Big> value_;
    static constexpr auto min = std::numeric_limits<std::int64_t>::min();
    static constexpr auto max = std::numeric_limits<std::int64_t>::max();

    Big big() const {
        if (const auto *v = std::get_if<std::int64_t>(&value_)) return Big(*v);
        return std::get<Big>(value_);
    }

    explicit Int(Big value) {
        if (value >= min && value <= max) value_ = value.convert_to<std::int64_t>();
        else value_ = std::move(value);
    }

    static int compare(const Int &a, const Int &b) {
        if (!a.is_big() && !b.is_big()) {
            auto x = std::get<std::int64_t>(a.value_);
            auto y = std::get<std::int64_t>(b.value_);
            return (x > y) - (x < y);
        }
        return a.big().compare(b.big());
    }

public:
    Int(std::int64_t value = 0) : value_(value) {}

    static Int parse(std::string_view text) {
        std::int64_t small = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), small);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
            return Int(small);
        }
        bool negative = !text.empty() && text.front() == '-';
        if (negative || (!text.empty() && text.front() == '+')) text.remove_prefix(1);
        if (text.empty()) throw std::invalid_argument("invalid integer");
        Big value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') throw std::invalid_argument("invalid integer");
            value *= 10;
            value += c - '0';
        }
        if (negative) value = -value;
        return Int(std::move(value));
    }

    std::size_t hash() const {
        if (is_big()) return boost::multiprecision::hash_value(std::get<Big>(value_));
        return std::hash<std::int64_t>{}(std::get<std::int64_t>(value_));
    }

    bool is_big() const { return std::holds_alternative<Big>(value_); }

    std::size_t to_size(const char *context) const {
        if (is_big()) throw std::out_of_range(std::string(context) + " is out of range");
        auto value = std::get<std::int64_t>(value_);
        if (value < 0 || static_cast<std::uint64_t>(value) >
                         std::numeric_limits<std::size_t>::max()) {
            throw std::out_of_range(std::string(context) + " is out of range");
        }
        return static_cast<std::size_t>(value);
    }

    explicit operator bool() const {
        if (!is_big()) return std::get<std::int64_t>(value_) != 0;
        return std::get<Big>(value_) != 0;
    }

    friend Int operator+(const Int &a, const Int &b) {
        if (!a.is_big() && !b.is_big()) {
            std::int64_t result;
            if (!__builtin_add_overflow(std::get<std::int64_t>(a.value_),
                                        std::get<std::int64_t>(b.value_), &result)) {
                return Int(result);
            }
        }
        return Int(Big(a.big() + b.big()));
    }

    friend Int operator-(const Int &a, const Int &b) {
        if (!a.is_big() && !b.is_big()) {
            std::int64_t result;
            if (!__builtin_sub_overflow(std::get<std::int64_t>(a.value_),
                                        std::get<std::int64_t>(b.value_), &result)) {
                return Int(result);
            }
        }
        return Int(Big(a.big() - b.big()));
    }

    friend Int operator*(const Int &a, const Int &b) {
        if (!a.is_big() && !b.is_big()) {
            std::int64_t result;
            if (!__builtin_mul_overflow(std::get<std::int64_t>(a.value_),
                                        std::get<std::int64_t>(b.value_), &result)) {
                return Int(result);
            }
        }
        return Int(Big(a.big() * b.big()));
    }

    friend Int operator/(const Int &a, const Int &b) {
        if (!static_cast<bool>(b)) throw std::domain_error("division by zero");
        if (!a.is_big() && !b.is_big()) {
            auto x = std::get<std::int64_t>(a.value_);
            auto y = std::get<std::int64_t>(b.value_);
            if (!(x == min && y == -1)) return Int(x / y);
        }
        return Int(Big(a.big() / b.big()));
    }

    friend Int operator%(const Int &a, const Int &b) {
        if (!b) throw std::domain_error("division by zero");
        if (!a.is_big() && !b.is_big()) {
            auto x = std::get<std::int64_t>(a.value_);
            auto y = std::get<std::int64_t>(b.value_);
            return Int(y == -1 ? 0 : x % y);
        }
        return Int(Big(a.big() % b.big()));
    }

    friend Int operator-(const Int &a) {
        if (!a.is_big()) {
            auto x = std::get<std::int64_t>(a.value_);
            if (x != min) return Int(-x);
        }
        return Int(Big(-a.big()));
    }

    friend Int operator==(const Int &a, const Int &b) { return Int(compare(a, b) == 0); }
    friend Int operator!=(const Int &a, const Int &b) { return Int(compare(a, b) != 0); }
    friend Int operator<(const Int &a, const Int &b) { return Int(compare(a, b) < 0); }
    friend Int operator<=(const Int &a, const Int &b) { return Int(compare(a, b) <= 0); }
    friend Int operator>(const Int &a, const Int &b) { return Int(compare(a, b) > 0); }
    friend Int operator>=(const Int &a, const Int &b) { return Int(compare(a, b) >= 0); }

    Int &operator+=(const Int &other) { *this = *this + other; return *this; }
    Int &operator-=(const Int &other) { *this = *this - other; return *this; }
    Int &operator*=(const Int &other) { *this = *this * other; return *this; }
    Int &operator%=(const Int &other) { *this = *this % other; return *this; }
    Int &operator/=(const Int &other) { *this = *this / other; return *this; }

    friend std::ostream &operator<<(std::ostream &out, const Int &value) {
        if (!value.is_big()) return out << std::get<std::int64_t>(value.value_);
        return out << std::get<Big>(value.value_);
    }
};

inline Int gcd(Int a, Int b) {
    while (b) {
        Int remainder = a % b;
        a = std::move(b);
        b = std::move(remainder);
    }
    return a < Int(0) ? -a : a;
}

inline Int lcm(const Int &a, const Int &b) {
    if (!a || !b) return Int(0);
    Int result = (a / gcd(a, b)) * b;
    return result < Int(0) ? -result : result;
}

inline Int normalize_mod(const Int &value, const Int &modulus) {
    Int remainder = value % modulus;
    return remainder < Int(0) ? remainder + modulus : remainder;
}

inline Int pow_mod(Int base, Int exponent, const Int &modulus) {
    if (modulus <= Int(0) || exponent < Int(0))
        throw std::domain_error("pow_mod requires a positive modulus and nonnegative exponent");
    base = normalize_mod(base, modulus);
    Int result = Int(1) % modulus;
    while (exponent) {
        if (exponent % Int(2)) result = (result * base) % modulus;
        exponent /= Int(2);
        if (exponent) base = (base * base) % modulus;
    }
    return result;
}

inline Int inv_mod(const Int &value, const Int &modulus) {
    if (modulus <= Int(0)) throw std::domain_error("inv_mod requires a positive modulus");
    Int a = modulus, b = normalize_mod(value, modulus), x(0), y(1);
    while (b) {
        Int quotient = a / b;
        Int next = a - quotient * b;
        a = std::move(b);
        b = std::move(next);
        next = x - quotient * y;
        x = std::move(y);
        y = std::move(next);
    }
    if (a != Int(1)) throw std::domain_error("value and modulus must be coprime");
    return normalize_mod(x, modulus);
}

template <class Modulus>
class Mint {
    Int value_;
    static const Int &modulus() { return Modulus::value(); }
public:
    Mint() = default;
    explicit Mint(const Int &value) : value_(normalize_mod(value, modulus())) {}
    const Int &val() const { return value_; }
    Mint pow(const Int &exponent) const { return Mint(pow_mod(value_, exponent, modulus())); }
    friend Mint operator+(const Mint &a, const Mint &b) { return Mint(a.value_ + b.value_); }
    friend Mint operator-(const Mint &a, const Mint &b) { return Mint(a.value_ - b.value_); }
    friend Mint operator*(const Mint &a, const Mint &b) { return Mint(a.value_ * b.value_); }
    friend Mint operator/(const Mint &a, const Mint &b) { return Mint(a.value_ * inv_mod(b.value_, modulus())); }
    friend Mint operator-(const Mint &a) { return Mint(-a.value_); }
    friend Int operator==(const Mint &a, const Mint &b) { return a.value_ == b.value_; }
    friend Int operator!=(const Mint &a, const Mint &b) { return a.value_ != b.value_; }
    Mint &operator+=(const Mint &b) { return *this = *this + b; }
    Mint &operator-=(const Mint &b) { return *this = *this - b; }
    Mint &operator*=(const Mint &b) { return *this = *this * b; }
    Mint &operator/=(const Mint &b) { return *this = *this / b; }
    friend std::ostream &operator<<(std::ostream &out, const Mint &value) { return out << value.value_; }
};

template <int Rank, class Element = Int> struct VectorType {
    using type = std::vector<typename VectorType<Rank - 1, Element>::type>;
};
template <> struct VectorType<1, char> { using type = std::string; };
template <class Element> struct VectorType<0, Element> { using type = Element; };
template <int Rank, class Element = Int> using Vec = typename VectorType<Rank, Element>::type;
using Array = Vec<1>;
using IntPair = std::pair<Int, Int>;

inline bool integer_less(const Int &left, const Int &right) {
    return static_cast<bool>(left < right);
}

inline void sort(Array &values) {
    std::sort(values.begin(), values.end(), integer_less);
}

inline Int at_least(const Array &values, const Int &x) {
    return Int(std::lower_bound(values.begin(), values.end(), x, integer_less) - values.begin());
}

inline Int more_than(const Array &values, const Int &x) {
    return Int(std::upper_bound(values.begin(), values.end(), x, integer_less) - values.begin());
}

inline Int at_most(const Array &values, const Int &x) {
    return more_than(values, x) - Int(1);
}

inline Int less_than(const Array &values, const Int &x) {
    return at_least(values, x) - Int(1);
}

inline std::vector<IntPair> run_length(const Array &values) {
    std::vector<IntPair> runs;
    for (const auto &value : values) {
        if (runs.empty() || runs.back().first != value) runs.emplace_back(value, Int(1));
        else runs.back().second += Int(1);
    }
    return runs;
}

template <int Rank, class Element = Int>
Vec<Rank, Element> make_vec(const std::vector<std::size_t> &shape, std::size_t dimension = 0) {
    if constexpr (Rank == 1 && std::is_same_v<Element, char>) {
        return std::string(shape[dimension], '\0');
    } else {
        Vec<Rank, Element> values(shape[dimension]);
        if constexpr (Rank > 1) {
            for (auto &child : values) child = make_vec<Rank - 1, Element>(shape, dimension + 1);
        }
        return values;
    }
}

inline std::string read_string() {
    std::string value;
    if (!(std::cin >> value)) throw std::runtime_error("expected string input");
    return value;
}

inline char read_char() {
    char value;
    if (!(std::cin >> value)) throw std::runtime_error("expected character input");
    return value;
}

inline Int len(const std::string &value) { return Int(static_cast<std::int64_t>(value.size())); }
inline Int size(const std::string &value) { return len(value); }
inline void push_back(std::string &value, char c) { value.push_back(c); }
inline void push(std::string &value, char c) { value.push_back(c); }
inline void clear(std::string &value) { value.clear(); }
inline void resize(std::string &value, const Int &n, char fill = '\0') {
    value.resize(n.to_size("string size"), fill);
}

inline char back(const std::string &value) {
    if (value.empty()) throw std::out_of_range("back of empty string");
    return value.back();
}
inline char pop(std::string &value) {
    char c = back(value);
    value.pop_back();
    return c;
}

inline char &at(std::string &value, const Int &i) { return value.at(i.to_size("string index")); }
inline const char &at(const std::string &value, const Int &i) { return value.at(i.to_size("string index")); }

inline Int read_int() {
    std::string token;
    if (!(std::cin >> token)) throw std::runtime_error("expected integer input");
    return Int::parse(token);
}

inline Array array(const Int &size, const Int &initial = Int(0)) {
    return Array(size.to_size("array size"), initial);
}

inline Array read_vec(const Int &size) {
    Array values = array(size);
    for (auto &value : values) value = read_int();
    return values;
}

template <class T>
Int len(const std::vector<T> &values) {
    return Int(static_cast<std::int64_t>(values.size()));
}

template <class T>
std::size_t checked_index(const std::vector<T> &values, const Int &index) {
    auto position = index.to_size("array index");
    if (position >= values.size()) throw std::out_of_range("array index is out of bounds");
    return position;
}

template <class T>
T &at(std::vector<T> &values, const Int &index) {
    return values[checked_index(values, index)];
}

template <class T>
const T &at(const std::vector<T> &values, const Int &index) {
    return values[checked_index(values, index)];
}

struct Edge {
    Int to, cost, id;
};

struct Diameter {
    Int dist, one, two;
};

template <bool Directed>
class Graph {
    std::vector<std::vector<Edge>> rows_;
    Int edge_count_;
public:
    explicit Graph(const Int &n = Int(0)) : rows_(n.to_size("vertex count")) {}
    Int size() const { return len(rows_); }
    const Int &edge_count() const { return edge_count_; }
    const std::vector<Edge> &neighbors(const Int &u) const { return at(rows_, u); }

    Array bfs(const Int &source) const {
        const auto start = checked_index(rows_, source);
        Array distance(rows_.size(), Int(-1));
        std::vector<std::size_t> queue{start};
        distance[start] = Int(0);
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const auto u = queue[i];
            for (const auto &edge : rows_[u]) {
                auto v = edge.to.to_size("vertex index");
                if (distance[v] != Int(-1)) continue;
                distance[v] = distance[u] + Int(1);
                queue.push_back(v);
            }
        }
        return distance;
    }

    Array dijkstra(const Int &source) const {
        const auto start = checked_index(rows_, source);
        using Entry = std::pair<Int, std::size_t>;
        auto greater = [](const Entry &a, const Entry &b) { return static_cast<bool>(a.first > b.first); };
        std::priority_queue<Entry, std::vector<Entry>, decltype(greater)> queue(greater);
        Array distance(rows_.size(), Int(-1));
        distance[start] = Int(0);
        queue.emplace(Int(0), start);
        while (!queue.empty()) {
            auto [cost, u] = queue.top();
            queue.pop();
            if (cost != distance[u]) continue;
            for (const auto &edge : rows_[u]) {
                if (edge.cost < Int(0)) throw std::domain_error("dijkstra requires nonnegative edge weights");
                auto v = edge.to.to_size("vertex index");
                Int candidate = cost + edge.cost;
                if (distance[v] != Int(-1) && candidate >= distance[v]) continue;
                distance[v] = candidate;
                queue.emplace(std::move(candidate), v);
            }
        }
        return distance;
    }

    Array components() const {
        Array component(rows_.size(), Int(-1));
        Int next(0);
        std::vector<std::size_t> queue;
        for (std::size_t start = 0; start < rows_.size(); ++start) {
            if (component[start] != Int(-1)) continue;
            component[start] = next;
            queue.assign(1, start);
            for (std::size_t i = 0; i < queue.size(); ++i) {
                for (const auto &edge : rows_[queue[i]]) {
                    auto v = edge.to.to_size("vertex index");
                    if (component[v] != Int(-1)) continue;
                    component[v] = next;
                    queue.push_back(v);
                }
            }
            next += Int(1);
        }
        return component;
    }

    Diameter diameter() const {
        const auto n = rows_.size();
        if (n == 0 || edge_count_ != Int(static_cast<std::int64_t>(n - 1)))
            throw std::invalid_argument("diameter requires a nonempty undirected tree");
        auto farthest = [&](std::size_t start) {
            std::vector<Int> dist(n, Int(-1));
            std::vector<std::size_t> order{start};
            dist[start] = Int(0);
            auto best = start;
            for (std::size_t i = 0; i < order.size(); ++i) {
                auto u = order[i];
                for (const auto &edge : rows_[u]) {
                    if (edge.cost < Int(0)) throw std::invalid_argument("diameter requires nonnegative edge weights");
                    auto v = edge.to.to_size("vertex index");
                    if (dist[v] >= Int(0)) continue;
                    dist[v] = dist[u] + edge.cost;
                    order.push_back(v);
                    if (best == start || dist[v] > dist[best]) best = v;
                }
            }
            if (order.size() != n) throw std::invalid_argument("diameter requires a connected tree");
            return std::pair(best, std::move(dist[best]));
        };
        auto [one, ignored] = farthest(0);
        auto [two, dist] = farthest(one);
        return {std::move(dist), Int(static_cast<std::int64_t>(one)), Int(static_cast<std::int64_t>(two))};
    }

    Int add(const Int &u, const Int &v, const Int &cost = Int(1)) {
        auto &from = at(rows_, u);
        auto &to = at(rows_, v);
        const Int id = edge_count_;
        Int next = id + Int(1);
        from.push_back({v, cost, id});
        if constexpr (!Directed) {
            try { to.push_back({u, cost, id}); }
            catch (...) { from.pop_back(); throw; }
        }
        edge_count_ = std::move(next);
        return id;
    }
};

template <bool Directed>
const std::vector<Edge> &at(const Graph<Directed> &graph, const Int &u) {
    return graph.neighbors(u);
}

template <class Key>
struct KeyLess {
    bool operator()(const Key &a, const Key &b) const {
        if constexpr (std::is_same_v<Key, char>)
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        return static_cast<bool>(a < b);
    }
};

template <class Key>
struct KeyEqual {
    bool operator()(const Key &a, const Key &b) const { return static_cast<bool>(a == b); }
};

template <class Key>
struct KeyHash {
    std::size_t operator()(const Key &key) const {
        if constexpr (std::is_same_v<Key, Int>) return key.hash();
        else return std::hash<Key>{}(key);
    }
};

template <int Kind, class Key, class Value>
class Associative {
    static constexpr bool is_map = Kind < 4;
    static constexpr bool multi = (Kind & 2) != 0;
    using Group = std::conditional_t<is_map, std::conditional_t<multi, Vec<1, Value>, Value>, std::size_t>;
    using Ordered = std::map<Key, Group, KeyLess<Key>>;
    using Unordered = std::unordered_map<Key, Group, KeyHash<Key>, KeyEqual<Key>>;
    std::conditional_t<(Kind & 1) != 0, Unordered, Ordered> groups_;
    std::size_t total_ = 0;

    static std::size_t occurrences(const Group &group) {
        if constexpr (!is_map) return group;
        else if constexpr (multi) return group.size();
        else return 1;
    }

public:
    Int size() const { return Int(static_cast<std::int64_t>(multi ? total_ : groups_.size())); }
    Int count(const Key &key) const {
        const auto found = groups_.find(key);
        return Int(found == groups_.end() ? 0 : static_cast<std::int64_t>(occurrences(found->second)));
    }
    Int contains(const Key &key) const { return Int(groups_.find(key) != groups_.end()); }
    Int erase(const Key &key) {
        const auto found = groups_.find(key);
        if (found == groups_.end()) return Int(0);
        auto removed = occurrences(found->second);
        groups_.erase(found);
        if constexpr (multi) total_ -= removed;
        return Int(static_cast<std::int64_t>(removed));
    }
    Vec<1, Key> keys() const {
        Vec<1, Key> keys;
        keys.reserve(groups_.size());
        for (const auto &[key, group] : groups_) keys.push_back(key);
        return keys;
    }

    void insert(Key key, Value value) requires is_map {
        if constexpr (multi) {
            auto [position, created] = groups_.try_emplace(std::move(key));
            try { position->second.push_back(std::move(value)); }
            catch (...) { if (created) groups_.erase(position); throw; }
            ++total_;
        } else {
            groups_.insert_or_assign(std::move(key), std::move(value));
        }
    }
    void insert(Key key) requires (!is_map) {
        auto &count = groups_[std::move(key)];
        if constexpr (multi) { ++count; ++total_; }
        else count = 1;
    }

    Value &index(const Key &key) requires (is_map && !multi) { return groups_[key]; }
    auto index(const Key &key) const {
        if constexpr (is_map) {
            const auto found = groups_.find(key);
            return found == groups_.end() ? Group{} : found->second;
        } else {
            return count(key);
        }
    }
};

template <int Kind, class Key, class Value>
decltype(auto) at(Associative<Kind, Key, Value> &values, const Key &key) { return values.index(key); }

template <int Kind, class Key, class Value>
auto at(const Associative<Kind, Key, Value> &values, const Key &key) { return values.index(key); }

template <class Values>
Values &at_path(Values &values) { return values; }

template <class Values, class... Rest>
decltype(auto) at_path(Values &values, const Int &index, const Rest &...rest) {
    return at_path(at(values, index), rest...);
}

template <class T>
void push(std::vector<T> &values, T value) { values.push_back(std::move(value)); }

template <class T>
T pop(std::vector<T> &values) {
    if (values.empty()) throw std::out_of_range("pop from empty vector");
    T result = std::move(values.back());
    values.pop_back();
    return result;
}

template <class T>
T back(const std::vector<T> &values) {
    if (values.empty()) throw std::out_of_range("back of empty vector");
    return values.back();
}

template <class T>
Int size(const std::vector<T> &values) { return len(values); }

template <class T>
void resize(std::vector<T> &values, const Int &count, T initial = T{}) {
    values.resize(count.to_size("vector size"), initial);
}

template <class T>
void clear(std::vector<T> &values) { values.clear(); }

template <class T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &values) {
    for (std::size_t i = 0; i < values.size(); i++) {
        if (i) out << ' ';
        if constexpr (std::is_same_v<T, Int> || std::is_same_v<T, std::string>) out << values[i];
        else out << '[' << values[i] << ']';
    }
    return out;
}

} // namespace fken
#endif
