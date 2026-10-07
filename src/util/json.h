// Minimal JSON value, parser and serializer.
//
// Self-contained on purpose: the PS5 payload SDK ships a bare libc++, and the
// xCloud protocol only needs plain objects, arrays, strings and numbers.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xc::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : type_(Type::Number), num_(v) {}
    Value(int64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Value(double v) : type_(Type::Number), num_(v) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
    double asNumber(double def = 0) const { return isNumber() ? num_ : def; }
    int64_t asInt(int64_t def = 0) const { return isNumber() ? static_cast<int64_t>(num_) : def; }
    const std::string& asString() const;
    std::string str(std::string def = {}) const { return isString() ? str_ : def; }

    // Arrays.
    size_t size() const;
    const Value& operator[](size_t i) const;
    const Value& operator[](int i) const { return i < 0 ? (*this)[size_t(-1)] : (*this)[size_t(i)]; }
    void push(Value v);
    const Array& items() const;

    // Objects. Missing keys on a const lookup yield a shared Null value, so
    // chains like v["a"]["b"].str() are safe.
    const Value& operator[](std::string_view key) const;
    const Value& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    Value& set(const std::string& key, Value v);
    bool has(std::string_view key) const;
    const Object& members() const;

    std::string dump() const;

private:
    void dumpTo(std::string& out) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::shared_ptr<Array> arr_;
    std::shared_ptr<Object> obj_;
};

// Returns nullopt on malformed input; `error` (if given) receives a message.
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

std::string escape(std::string_view s);

}  // namespace xc::json
