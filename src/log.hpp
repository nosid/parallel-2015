#pragma once

#include <iostream>
#include <sstream>

namespace demo
{

    void log_aux(std::ostream& os __attribute__((unused))) { }

    template <typename Head, typename... Tail>
    void log_aux(std::ostream& os, Head&& head, Tail&&... tail)
    {
        os << std::forward<Head>(head);
        log_aux(os, std::forward<Tail>(tail)...);
    }

    template <typename... Args>
    void log(Args&&... args)
    {
        std::ostringstream os;
        log_aux(os, std::forward<Args>(args)..., '\n');
        std::cerr << os.str();
    }

}
