#ifndef FKEN_RUNTIME_HPP
#define FKEN_RUNTIME_HPP

#include <boost/multiprecision/cpp_int.hpp>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

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
        if (negative) text.remove_prefix(1);
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

    friend std::ostream &operator<<(std::ostream &out, const Int &value) {
        if (!value.is_big()) return out << std::get<std::int64_t>(value.value_);
        return out << std::get<Big>(value.value_);
    }
};

} // namespace fken
#endif
