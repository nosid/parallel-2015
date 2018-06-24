#pragma once

#include <type_traits>

namespace demo
{

    template <typename Amount, typename Parts>
    auto partitioner(Amount amount, Parts parts)
    {
        static_assert(std::is_integral_v<Amount>);
        static_assert(std::is_integral_v<Parts>);
        return [parts,amount]() mutable {
            auto result = amount / parts;
            --parts;
            amount -= result;
            return result;
        };
    }

}
