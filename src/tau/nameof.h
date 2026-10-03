#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace tau
{
    namespace nameof_detail
    {
        constexpr std::string_view bracketed(std::string_view sig)
        {
            const std::size_t begin = sig.find(" = ");
            const std::size_t end = sig.rfind(']');

            if (begin == std::string_view::npos || end == std::string_view::npos || end <= begin + 3) { return {}; }

            return sig.substr(begin + 3, end - begin - 3);
        }

        constexpr std::string_view unqualified(std::string_view qualified)
        {
            const std::size_t sep = qualified.rfind("::");
            return sep == std::string_view::npos ? qualified : qualified.substr(sep + 2);
        }

        template <auto V>
        constexpr std::string_view value_sig()
        { return __PRETTY_FUNCTION__; }

        template <typename T>
        constexpr std::string_view type_sig()
        { return __PRETTY_FUNCTION__; }
    } // namespace nameof_detail

    template <auto MemberPtr>
    constexpr std::string_view member_name()
    { return nameof_detail::unqualified(nameof_detail::bracketed(nameof_detail::value_sig<MemberPtr>())); }

    template <auto EnumValue>
    constexpr std::string_view enum_name()
    {
        const std::string_view qualified = nameof_detail::bracketed(nameof_detail::value_sig<EnumValue>());

        if (qualified.empty() || qualified.front() == '(') { return {}; }

        return nameof_detail::unqualified(qualified);
    }

    template <typename T>
    constexpr std::string_view type_name()
    { return nameof_detail::unqualified(nameof_detail::bracketed(nameof_detail::type_sig<T>())); }

    template <std::size_t N>
    struct fixed_name_t
    {
        std::array<char, N + 1> buffer{};

        constexpr explicit fixed_name_t(std::string_view text)
        {
            for (std::size_t i = 0; i < N; ++i) { buffer[i] = text[i]; }
            buffer[N] = '\0';
        }

        constexpr const char* c_str() const { return buffer.data(); }
        constexpr std::string_view view() const { return std::string_view(buffer.data(), N); }
    };

    template <auto MemberPtr>
    inline constexpr auto member_label = fixed_name_t<member_name<MemberPtr>().size()>{member_name<MemberPtr>()};

    template <typename T>
    inline constexpr auto type_label = fixed_name_t<type_name<T>().size()>{type_name<T>()};

    template <auto EnumValue>
    inline constexpr auto enum_label = fixed_name_t<enum_name<EnumValue>().size()>{enum_name<EnumValue>()};

    namespace nameof_detail
    {
        namespace probe
        {
            struct outer_t
            {
                int alpha;
                bool beta;
            };

            enum class kind_e : unsigned char
            {
                FIRST,
                SECOND
            };

            static_assert(member_name<&outer_t::alpha>() == "alpha");
            static_assert(member_name<&outer_t::beta>() == "beta");
            static_assert(type_name<outer_t>() == "outer_t");
            static_assert(type_name<kind_e>() == "kind_e");
            static_assert(enum_name<kind_e::FIRST>() == "FIRST");
            static_assert(enum_name<kind_e::SECOND>() == "SECOND");
            static_assert(enum_name<static_cast<kind_e>(7)>().empty());

            static_assert(member_label<&outer_t::alpha>.view() == "alpha");
            static_assert(type_label<outer_t>.view() == "outer_t");
            static_assert(enum_label<kind_e::SECOND>.view() == "SECOND");
        } // namespace probe
    } // namespace nameof_detail
} // namespace tau
