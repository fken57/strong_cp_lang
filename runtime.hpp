#ifndef FKEN_RUNTIME_HPP
#define FKEN_RUNTIME_HPP

#include <boost/multiprecision/cpp_int.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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
    Int &operator/=(const Int &other) { *this = *this / other; return *this; }

    friend std::ostream &operator<<(std::ostream &out, const Int &value) {
        if (!value.is_big()) return out << std::get<std::int64_t>(value.value_);
        return out << std::get<Big>(value.value_);
    }
};

template <int Rank> struct VectorType { using type = std::vector<typename VectorType<Rank - 1>::type>; };
template <> struct VectorType<0> { using type = Int; };
template <int Rank> using Vec = typename VectorType<Rank>::type;
using Array = Vec<1>;

template <int Rank>
Vec<Rank> make_vec(const std::vector<std::size_t> &shape, std::size_t dimension = 0) {
    Vec<Rank> values(shape[dimension]);
    if constexpr (Rank > 1) {
        for (auto &child : values) child = make_vec<Rank - 1>(shape, dimension + 1);
    }
    return values;
}

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
        if constexpr (std::is_same_v<T, Int>) out << values[i];
        else out << '[' << values[i] << ']';
    }
    return out;
}

} // namespace fken
#endif
