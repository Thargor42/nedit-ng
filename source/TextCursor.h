
#ifndef TEXT_CURSOR_H_
#define TEXT_CURSOR_H_

#include <cstdint>
#include <type_traits>

#if 1

template <class Integer>
using IsInteger = typename std::enable_if<std::is_integral<Integer>::value>::type;

template <class T>
class BasicTextCursor {
public:
    using underlying_type = T;

public:
    template <class Integer, class = IsInteger<T>>
    constexpr explicit BasicTextCursor(Integer index) noexcept : index_(index) { }

public:
    BasicTextCursor()                                   = default;
    BasicTextCursor(const BasicTextCursor &)            = default;
    BasicTextCursor& operator=(const BasicTextCursor &) = default;
    BasicTextCursor(BasicTextCursor &&)                 = default;
    BasicTextCursor& operator=(BasicTextCursor &&)      = default;
    ~BasicTextCursor()                                  = default;

public:
    constexpr BasicTextCursor& operator++() noexcept   { ++index_; return *this; }
    constexpr BasicTextCursor& operator--() noexcept   { --index_; return *this; }
    constexpr BasicTextCursor operator++(int) noexcept { BasicTextCursor tmp(*this); ++index_; return tmp; }
    constexpr BasicTextCursor operator--(int) noexcept { BasicTextCursor tmp(*this); --index_; return tmp; }

public:
    constexpr T operator-(const BasicTextCursor &rhs) const noexcept { return index_ - rhs.index_; }

public:
    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor& operator+=(Integer n) noexcept { index_ += n; return *this; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor& operator-=(Integer n) noexcept { index_ -= n; return *this; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor& operator*=(Integer n) noexcept { index_ *= n; return *this; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor& operator/=(Integer n) noexcept { index_ /= n; return *this; }

public:
    constexpr bool operator==(const BasicTextCursor &rhs) const noexcept { return index_ == rhs.index_; }
    constexpr bool operator!=(const BasicTextCursor &rhs) const noexcept { return index_ != rhs.index_; }
    constexpr bool operator< (const BasicTextCursor &rhs) const noexcept { return index_ <  rhs.index_; }
    constexpr bool operator<=(const BasicTextCursor &rhs) const noexcept { return index_ <= rhs.index_; }
    constexpr bool operator> (const BasicTextCursor &rhs) const noexcept { return index_ >  rhs.index_; }
    constexpr bool operator>=(const BasicTextCursor &rhs) const noexcept { return index_ >= rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator==(Integer rhs) const noexcept { return index_ == rhs; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator!=(Integer rhs) const noexcept { return index_ != rhs; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator< (Integer rhs) const noexcept { return index_ <  rhs; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator<=(Integer rhs) const noexcept { return index_ <= rhs; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator> (Integer rhs) const noexcept { return index_ >  rhs; }

    template <class Integer, class = IsInteger<T>>
    constexpr bool operator>=(Integer rhs) const noexcept { return index_ >= rhs; }

public:
    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator==(Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs == rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator!=(Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs != rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator< (Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs <  rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator<=(Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs <= rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator> (Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs >  rhs.index_; }

    template <class Integer, class = IsInteger<T>>
    friend constexpr bool operator>=(Integer lhs, const BasicTextCursor &rhs) noexcept { return lhs >= rhs.index_; }

public:
    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor operator+(Integer n) const noexcept { BasicTextCursor tmp(*this); tmp += n; return tmp; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor operator-(Integer n) const noexcept { BasicTextCursor tmp(*this); tmp -= n; return tmp; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor operator*(Integer n) const noexcept { BasicTextCursor tmp(*this); tmp *= n; return tmp; }

    template <class Integer, class = IsInteger<T>>
    constexpr BasicTextCursor operator/(Integer n) const noexcept { BasicTextCursor tmp(*this); tmp /= n; return tmp; }

public:
    template <class Integer, class = IsInteger<T>>
    friend constexpr BasicTextCursor operator+(Integer lhs, BasicTextCursor rhs)  noexcept { BasicTextCursor tmp(rhs); tmp += lhs; return tmp; }

public:
    friend T to_integer(BasicTextCursor cursor) {
        return cursor.index_;
    }

private:
    T index_ = 0;
};

using TextCursor = BasicTextCursor<int32_t>;
#else
using TextCursor = int64_t;

inline int64_t to_integer(TextCursor cursor) {
    return cursor;
}

#endif

#endif
