#pragma once

#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace simagv::json {

struct Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

struct Value {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Storage storage; // JSON存储

    Value() : storage(nullptr) {}
    Value(std::nullptr_t) : storage(nullptr) {}
    Value(bool value) : storage(value) {}
    Value(double value) : storage(value) {}
    Value(const char* value) : storage(std::string(value == nullptr ? "" : value)) {}
    Value(std::string value) : storage(std::move(value)) {}
    Value(Array value) : storage(std::move(value)) {}
    Value(Object value) : storage(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(storage); }
    bool isBool() const { return std::holds_alternative<bool>(storage); }
    bool isNumber() const { return std::holds_alternative<double>(storage); }
    bool isString() const { return std::holds_alternative<std::string>(storage); }
    bool isArray() const { return std::holds_alternative<Array>(storage); }
    bool isObject() const { return std::holds_alternative<Object>(storage); }

    const bool& asBool() const { return std::get<bool>(storage); }
    const double& asNumber() const { return std::get<double>(storage); }
    const std::string& asString() const { return std::get<std::string>(storage); }
    const Array& asArray() const { return std::get<Array>(storage); }
    const Object& asObject() const { return std::get<Object>(storage); }

    Array& asArray() { return std::get<Array>(storage); }
    Object& asObject() { return std::get<Object>(storage); }
};

class Parser final {
public:
    explicit Parser(std::string_view input) : input_(input), pos_(0) {}

    Value parse() {
        skipWs(); // 跳过空白
        Value outValue = parseValue(); // 解析值
        skipWs(); // 跳过空白
        if (pos_ != input_.size()) {
            throw std::runtime_error("json: trailing characters"); // 解析剩余
        }
        return outValue; // 返回值
    }

private:
    std::string_view input_; // 输入
    size_t pos_;             // 当前位置

    void skipWs() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_; // 前进
        }
    }

    char peek() const {
        if (pos_ >= input_.size()) {
            return '\0'; // 结束
        }
        return input_[pos_]; // 当前字符
    }

    char get() {
        if (pos_ >= input_.size()) {
            throw std::runtime_error("json: unexpected end"); // 越界
        }
        return input_[pos_++]; // 取字符
    }

    void expect(char expectedChar) {
        char gotChar = get(); // 取字符
        if (gotChar != expectedChar) {
            throw std::runtime_error("json: unexpected character"); // 不匹配
        }
    }

    Value parseValue() {
        skipWs(); // 跳过空白
        char ch = peek(); // 首字符
        if (ch == '{') {
            return Value{parseObject()}; // 对象
        }
        if (ch == '[') {
            return Value{parseArray()}; // 数组
        }
        if (ch == '"') {
            return Value{parseString()}; // 字符串
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return Value{parseNumber()}; // 数字
        }
        return parseLiteral(); // 字面量
    }

    Object parseObject() {
        Object outObj; // 输出对象
        expect('{'); // 开始对象
        skipWs(); // 空白
        if (peek() == '}') {
            get(); // 空对象
            return outObj; // 返回
        }
        while (true) {
            skipWs(); // 空白
            std::string key = parseString(); // 键
            skipWs(); // 空白
            expect(':'); // 分隔符
            skipWs(); // 空白
            Value value = parseValue(); // 值
            outObj.emplace(std::move(key), std::move(value)); // 写入
            skipWs(); // 空白
            char sep = get(); // 分隔
            if (sep == '}') {
                break; // 结束对象
            }
            if (sep != ',') {
                throw std::runtime_error("json: expected ',' or '}'"); // 错误
            }
        }
        return outObj; // 返回对象
    }

    Array parseArray() {
        Array outArr; // 输出数组
        expect('['); // 开始数组
        skipWs(); // 空白
        if (peek() == ']') {
            get(); // 空数组
            return outArr; // 返回
        }
        while (true) {
            skipWs(); // 空白
            Value value = parseValue(); // 元素
            outArr.emplace_back(std::move(value)); // 追加
            skipWs(); // 空白
            char sep = get(); // 分隔
            if (sep == ']') {
                break; // 结束
            }
            if (sep != ',') {
                throw std::runtime_error("json: expected ',' or ']'"); // 错误
            }
        }
        return outArr; // 返回
    }

    std::string parseString() {
        expect('"'); // 开始字符串
        std::string outStr; // 输出字符串
        while (true) {
            char ch = get(); // 取字符
            if (ch == '"') {
                break; // 结束字符串
            }
            if (ch == '\\') {
                char esc = get(); // 转义字符
                outStr.push_back(parseEscape(esc)); // 写入
                continue;
            }
            outStr.push_back(ch); // 普通字符
        }
        return outStr; // 返回
    }

    char parseEscape(char escChar) {
        if (escChar == '"' || escChar == '\\' || escChar == '/') {
            return escChar; // 直接返回
        }
        if (escChar == 'b') {
            return '\b'; // 退格
        }
        if (escChar == 'f') {
            return '\f'; // 换页
        }
        if (escChar == 'n') {
            return '\n'; // 换行
        }
        if (escChar == 'r') {
            return '\r'; // 回车
        }
        if (escChar == 't') {
            return '\t'; // 制表
        }
        if (escChar == 'u') {
            for (int i = 0; i < 4; ++i) {
                (void)get(); // 跳过4位
            }
            return '?'; // 降级处理
        }
        throw std::runtime_error("json: invalid escape"); // 错误
    }

    double parseNumber() {
        size_t startPos = pos_; // 起点
        if (peek() == '-') {
            ++pos_; // 负号
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_; // 整数部分
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_; // 小数点
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_; // 小数部分
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_; // 指数标记
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_; // 指数符号
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_; // 指数数字
            }
        }
        std::string_view numView = input_.substr(startPos, pos_ - startPos); // 数字文本
        try {
            return std::stod(std::string(numView)); // 转换
        } catch (...) {
            throw std::runtime_error("json: invalid number"); // 错误
        }
    }

    Value parseLiteral() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4; // 前进
            return Value{nullptr}; // 返回null
        }
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4; // 前进
            return Value{true}; // 返回true
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5; // 前进
            return Value{false}; // 返回false
        }
        throw std::runtime_error("json: invalid literal"); // 错误
    }
};

inline Value parse(std::string_view input) {
    Parser parser(input); // 解析器
    return parser.parse(); // 解析
}

} // namespace simagv::json
