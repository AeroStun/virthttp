#pragma once

#include <rapidxml_ns.hpp>
#include <string_view>

namespace virtxml {
using namespace rapidxml_ns;

struct Value {
    constexpr Value(xml_base<>* item) : item(item) {}

#if 1 /* With functions */

    constexpr explicit operator bool() const noexcept { return item != nullptr; }


#else /* With paramexpr */

    using operator bool(this s) = s.item != nullptr;
    using operator std::string_view(this s) = std::string_view{item->value(), item->value_size()};

#endif

  protected:
    xml_base<>* item;
};

struct String : public Value {
    inline explicit operator std::string_view() const noexcept { return {item->value(), item->value_size()}; }
};

struct Integral : public Value  {
    inline explicit operator int() const noexcept { return std::atoi(item->value()); }
};
} // namespace virtxml