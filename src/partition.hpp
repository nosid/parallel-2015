#pragma once

namespace demo
{

    template <typename Amount, typename Parts>
    auto partitioner(Amount amount, Parts parts)
    {
        return [parts,amount]() mutable {
            auto result = amount / parts;
            --parts;
            amount -= result;
            return result;
        };
    }

}
