#pragma once
#include <string>
#include <cstddef>

namespace xs_detail {

    template<size_t N, unsigned char BaseKey>
    struct xored_str {
        char data[N]{};

        constexpr xored_str(const char (&str)[N]) {
            for (size_t i = 0; i < N; ++i)
                data[i] = static_cast<char>(str[i] ^ static_cast<unsigned char>(BaseKey + i * 3));
        }

        inline std::string get() const {
            std::string s(N - 1, '\0');
            for (size_t i = 0; i < N - 1; ++i)
                s[i] = data[i] ^ static_cast<unsigned char>(BaseKey + i * 3);
            return s;
        }
    };

}

#define _XS_KEY(ctr)  static_cast<unsigned char>(((static_cast<unsigned>(ctr) * 97u + 0x5Bu) % 247u) + 5u)

#define XS(str) \
    ([]() -> std::string { \
        constexpr static xs_detail::xored_str<sizeof(str), _XS_KEY(__COUNTER__)> _xs(str); \
        return _xs.get(); \
    }())
