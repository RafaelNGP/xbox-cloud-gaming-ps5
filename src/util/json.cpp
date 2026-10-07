#include "util/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace xc::json {

namespace {
const Value kNull;
const std::string kEmpty;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<Value> run(std::string* error) {
        auto v = value(0);
        ws();
        if (v && pos_ != t_.size()) {
            fail("trailing characters");
            v.reset();
        }
        if (!v && error) *error = err_ + " at offset " + std::to_string(pos_);
        return v;
    }

private:
    void ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r'))
            ++pos_;
    }
    bool fail(const char* m) {
        if (err_.empty()) err_ = m;
        return false;
    }
    bool lit(std::string_view w) {
        if (t_.substr(pos_, w.size()) != w) return fail("bad literal");
        pos_ += w.size();
        return true;
    }

    std::optional<Value> value(int depth) {
        if (depth > 128) { fail("nesting too deep"); return std::nullopt; }
        ws();
        if (pos_ >= t_.size()) { fail("unexpected end"); return std::nullopt; }
        char c = t_[pos_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') {
            std::string s;
            if (!string(s)) return std::nullopt;
            return Value(std::move(s));
        }
        if (c == 't') { if (!lit("true")) return std::nullopt; return Value(true); }
        if (c == 'f') { if (!lit("false")) return std::nullopt; return Value(false); }
        if (c == 'n') { if (!lit("null")) return std::nullopt; return Value(); }
        return number();
    }

    std::optional<Value> number() {
        size_t start = pos_;
        if (pos_ < t_.size() && (t_[pos_] == '-' || t_[pos_] == '+')) ++pos_;
        while (pos_ < t_.size()) {
            char c = t_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') ++pos_;
            else break;
        }
        if (start == pos_) { fail("unexpected character"); return std::nullopt; }
        std::string num(t_.substr(start, pos_ - start));
        char* end = nullptr;
        double d = std::strtod(num.c_str(), &end);
        if (!end || *end) { fail("bad number"); return std::nullopt; }
        return Value(d);
    }

    bool hex4(uint32_t& out) {
        if (pos_ + 4 > t_.size()) return fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= c - '0';
            else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < t_.size()) {
            char c = t_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= t_.size()) break;
            char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.substr(pos_, 2) == "\\u") {
                        pos_ += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    std::optional<Value> array(int depth) {
        ++pos_;
        Value::Array a;
        ws();
        if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return Value(std::move(a)); }
        for (;;) {
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            a.push_back(std::move(*v));
            ws();
            if (pos_ >= t_.size()) { fail("unterminated array"); return std::nullopt; }
            char c = t_[pos_++];
            if (c == ']') return Value(std::move(a));
            if (c != ',') { fail("expected , or ]"); return std::nullopt; }
        }
    }

    std::optional<Value> object(int depth) {
        ++pos_;
        Value::Object o;
        ws();
        if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return Value(std::move(o)); }
        for (;;) {
            ws();
            if (pos_ >= t_.size() || t_[pos_] != '"') { fail("expected key"); return std::nullopt; }
            std::string key;
            if (!string(key)) return std::nullopt;
            ws();
            if (pos_ >= t_.size() || t_[pos_++] != ':') { fail("expected :"); return std::nullopt; }
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            o[std::move(key)] = std::move(*v);
            ws();
            if (pos_ >= t_.size()) { fail("unterminated object"); return std::nullopt; }
            char c = t_[pos_++];
            if (c == '}') return Value(std::move(o));
            if (c != ',') { fail("expected , or }"); return std::nullopt; }
        }
    }

    std::string_view t_;
    size_t pos_ = 0;
    std::string err_;
};
}  // namespace

const std::string& Value::asString() const { return isString() ? str_ : kEmpty; }

size_t Value::size() const {
    if (isArray()) return arr_->size();
    if (isObject()) return obj_->size();
    return 0;
}

const Value& Value::operator[](size_t i) const {
    if (!isArray() || i >= arr_->size()) return kNull;
    return (*arr_)[i];
}

void Value::push(Value v) {
    if (!isArray()) { *this = array(); }
    // Copy-on-write: values share storage after copies.
    if (arr_.use_count() > 1) arr_ = std::make_shared<Array>(*arr_);
    arr_->push_back(std::move(v));
}

const Value::Array& Value::items() const { return isArray() ? *arr_ : kEmptyArray; }

const Value& Value::operator[](std::string_view key) const {
    if (!isObject()) return kNull;
    auto it = obj_->find(std::string(key));
    return it == obj_->end() ? kNull : it->second;
}

Value& Value::set(const std::string& key, Value v) {
    if (!isObject()) { *this = object(); }
    if (obj_.use_count() > 1) obj_ = std::make_shared<Object>(*obj_);
    (*obj_)[key] = std::move(v);
    return *this;
}

bool Value::has(std::string_view key) const {
    return isObject() && obj_->count(std::string(key)) != 0;
}

const Value::Object& Value::members() const { return isObject() ? *obj_ : kEmptyObject; }

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void Value::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[32];
            if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 9.007199254740992e15)
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(num_));
            else
                std::snprintf(buf, sizeof buf, "%.17g", num_);
            out += buf;
            break;
        }
        case Type::String: out += '"'; out += escape(str_); out += '"'; break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& v : *arr_) {
                if (!first) out += ',';
                first = false;
                v.dumpTo(out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, v] : *obj_) {
                if (!first) out += ',';
                first = false;
                out += '"';
                out += escape(k);
                out += "\":";
                v.dumpTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string Value::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

std::optional<Value> parse(std::string_view text, std::string* error) {
    return Parser(text).run(error);
}

}  // namespace xc::json
