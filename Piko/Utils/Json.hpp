#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace piko::utils::json
{
    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    class Value
    {
    public:
        using Array = std::vector<Value>;
        using Object = std::unordered_map<std::string, Value>;

        Value() = default;

        [[nodiscard]] Type type() const;
        [[nodiscard]] bool is_null() const;
        [[nodiscard]] bool is_boolean() const;
        [[nodiscard]] bool is_number() const;
        [[nodiscard]] bool is_string() const;
        [[nodiscard]] bool is_array() const;
        [[nodiscard]] bool is_object() const;

        [[nodiscard]] bool boolean() const;
        [[nodiscard]] double number() const;
        [[nodiscard]] const std::string &string() const;
        [[nodiscard]] const Array &array() const;
        [[nodiscard]] const Object &object() const;

        [[nodiscard]] const Value *find( std::string_view key ) const;
        [[nodiscard]] const Value &require( std::string_view key ) const;

        [[nodiscard]] std::string string_or( std::string fallback = {} ) const;
        [[nodiscard]] double number_or( double fallback ) const;
        [[nodiscard]] std::int64_t integer_or( std::int64_t fallback ) const;
        [[nodiscard]] bool boolean_or( bool fallback ) const;

        static Value make_null();
        static Value make_boolean( bool value );
        static Value make_number( double value );
        static Value make_string( std::string value );
        static Value make_array( Array value );
        static Value make_object( Object value );

    private:
        Type type_ = Type::Null;
        bool boolean_ = false;
        double number_ = 0.0;
        std::string string_{};
        Array array_{};
        Object object_{};
    };

    [[nodiscard]] Value parse( std::string_view text );
    [[nodiscard]] Value load_file( const std::filesystem::path &path );
}
