#pragma once

namespace demo
{

    template <typename Amount, typename Parts>
    auto partition(Amount amount, Parts parts)
    {
        return [parts,amount]() mutable {
            auto result = amount / parts;
            --parts;
            amount -= result;
            return result;
        };
    }

}
