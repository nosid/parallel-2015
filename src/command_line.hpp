#pragma once

#include <iomanip>
#include <string>
#include <vector>

namespace demo
{

    template <typename Result>
    auto parse_command_line_arg(const std::string& arg, Result& result)
        -> std::enable_if_t<std::is_integral<Result>::value>
    {
        std::istringstream is(arg);
        is >> result;
    }

    template <typename Value>
    auto format_command_line_arg(std::ostream& os, const Value& value)
        -> std::enable_if_t<std::is_integral<Value>::value>
    {
        os << value;
    }

    inline void parse_command_line_arg(const std::string& arg, std::string& result)
    {
        result = arg;
    }

    inline void format_command_line_arg(std::ostream& os, const std::string& value)
    {
        os << std::quoted(value);
    }

    template <typename Value>
    auto parse_command_line_arg(const std::string& arg, std::vector<Value>& result)
    {
        std::vector<Value> temp;
        auto p = arg.begin(), q = arg.end(), r = std::find(p, q, ',');
        while (r != q) {
            temp.emplace_back();
            parse_command_line_arg(std::string(p, r), temp.back());
            p = std::next(r);
            r = std::find(p, q, ',');
        }
        temp.emplace_back();
        parse_command_line_arg(std::string(p, q), temp.back());
        swap(temp, result);
    }

    template <typename Value>
    void format_command_line_arg(std::ostream& os, const std::vector<Value>& values)
    {
        os << '{';
        auto p = values.begin(), q = values.end();
        if (p != q) {
            format_command_line_arg(os, *p);
            for (++p; p != q; ++p) {
                os << ',';
                format_command_line_arg(os, *p);
            }
        }
        os << '}';
    }


    template <typename... Args>
    void parse_command_line(std::ostream& os, int argc, char* argv[] [[maybe_unused]])
    {
        os << std::flush;
        if (argc > 0) {
            throw std::runtime_error("too many command line arguments");
        }
    }

    template <typename Head, typename... Tail>
    void parse_command_line(std::ostream& os, int argc, char* argv[], std::string_view name, Head&& head, Tail&&... tail)
    {
        if (argc >= 1) {
            parse_command_line_arg(argv[0], head);
        }
        os << "PARAM: " << name << "=";
        format_command_line_arg(os, head);
        os << "\n";
        parse_command_line(os, argc - 1, argv + 1, std::forward<Tail>(tail)...);
    }

}
