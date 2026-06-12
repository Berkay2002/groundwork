#include "assets/AssetManifest.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};

void setError(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}

class Parser {
public:
    explicit Parser(std::string source) : source_(std::move(source)) {}

    bool parse(Json& out, std::string* error) {
        skipWs();
        if (!parseValue(out, error)) return false;
        skipWs();
        if (pos_ != source_.size()) {
            setError(error, "trailing characters after JSON document");
            return false;
        }
        return true;
    }

private:
    std::string source_;
    size_t pos_ = 0;

    void skipWs() {
        while (pos_ < source_.size() &&
               std::isspace(static_cast<unsigned char>(source_[pos_])))
            ++pos_;
    }

    bool consume(char c) {
        skipWs();
        if (pos_ >= source_.size() || source_[pos_] != c) return false;
        ++pos_;
        return true;
    }

    bool parseValue(Json& out, std::string* error) {
        skipWs();
        if (pos_ >= source_.size()) {
            setError(error, "unexpected end of JSON");
            return false;
        }
        char c = source_[pos_];
        if (c == '"') return parseStringValue(out, error);
        if (c == '{') return parseObject(out, error);
        if (c == '[') return parseArray(out, error);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return parseNumber(out, error);
        if (source_.compare(pos_, 4, "true") == 0) {
            pos_ += 4; out.type = Json::Type::Bool; out.boolean = true; return true;
        }
        if (source_.compare(pos_, 5, "false") == 0) {
            pos_ += 5; out.type = Json::Type::Bool; out.boolean = false; return true;
        }
        if (source_.compare(pos_, 4, "null") == 0) {
            pos_ += 4; out.type = Json::Type::Null; return true;
        }
        setError(error, "unexpected JSON token");
        return false;
    }

    bool parseString(std::string& out, std::string* error) {
        if (!consume('"')) {
            setError(error, "expected string");
            return false;
        }
        out.clear();
        while (pos_ < source_.size()) {
            char c = source_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= source_.size()) {
                    setError(error, "unterminated string escape");
                    return false;
                }
                char e = source_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default:
                        setError(error, "unsupported string escape");
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        setError(error, "unterminated string");
        return false;
    }

    bool parseStringValue(Json& out, std::string* error) {
        out.type = Json::Type::String;
        return parseString(out.text, error);
    }

    bool parseNumber(Json& out, std::string* error) {
        skipWs();
        size_t start = pos_;
        if (source_[pos_] == '-') ++pos_;
        while (pos_ < source_.size() &&
               std::isdigit(static_cast<unsigned char>(source_[pos_])))
            ++pos_;
        if (pos_ < source_.size() && source_[pos_] == '.') {
            ++pos_;
            while (pos_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[pos_])))
                ++pos_;
        }
        if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-'))
                ++pos_;
            while (pos_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[pos_])))
                ++pos_;
        }
        try {
            out.type = Json::Type::Number;
            out.number = std::stod(source_.substr(start, pos_ - start));
            return true;
        } catch (...) {
            setError(error, "bad number");
            return false;
        }
    }

    bool parseArray(Json& out, std::string* error) {
        if (!consume('[')) return false;
        out.type = Json::Type::Array;
        skipWs();
        if (consume(']')) return true;
        while (true) {
            Json item;
            if (!parseValue(item, error)) return false;
            out.array.push_back(std::move(item));
            skipWs();
            if (consume(']')) return true;
            if (!consume(',')) {
                setError(error, "expected ',' or ']' in array");
                return false;
            }
        }
    }

    bool parseObject(Json& out, std::string* error) {
        if (!consume('{')) return false;
        out.type = Json::Type::Object;
        skipWs();
        if (consume('}')) return true;
        while (true) {
            std::string key;
            if (!parseString(key, error)) return false;
            if (!consume(':')) {
                setError(error, "expected ':' after object key");
                return false;
            }
            Json value;
            if (!parseValue(value, error)) return false;
            out.object[key] = std::move(value);
            skipWs();
            if (consume('}')) return true;
            if (!consume(',')) {
                setError(error, "expected ',' or '}' in object");
                return false;
            }
        }
    }
};

const Json* member(const Json& object, const char* key) {
    auto it = object.object.find(key);
    return it == object.object.end() ? nullptr : &it->second;
}

bool isPathTraversal(const std::filesystem::path& p) {
    for (const auto& part : p) {
        if (part == "..") return true;
    }
    return false;
}

bool readTextFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

}

const ModelManifestEntry* AssetManifest::modelById(const std::string& id) const {
    for (const ModelManifestEntry& model : models)
        if (model.id == id) return &model;
    return nullptr;
}

bool loadAssetManifest(const std::filesystem::path& path,
                       AssetManifest& out,
                       std::string* error) {
    std::string source;
    if (!readTextFile(path, source)) {
        setError(error, "missing manifest: " + path.string());
        return false;
    }

    Json root;
    Parser parser(std::move(source));
    if (!parser.parse(root, error)) return false;
    if (root.type != Json::Type::Object) {
        setError(error, "manifest root must be an object");
        return false;
    }

    const Json* version = member(root, "version");
    if (!version || version->type != Json::Type::Number ||
        std::floor(version->number) != version->number || int(version->number) != 1) {
        setError(error, "manifest version must be 1");
        return false;
    }
    const Json* models = member(root, "models");
    if (!models || models->type != Json::Type::Array) {
        setError(error, "manifest models must be an array");
        return false;
    }

    AssetManifest parsed;
    parsed.version = 1;
    std::set<std::string> ids;
    std::filesystem::path manifestDir = path.parent_path();
    for (const Json& item : models->array) {
        if (item.type != Json::Type::Object) {
            setError(error, "model entry must be an object");
            return false;
        }
        const Json* id = member(item, "id");
        const Json* rel = member(item, "path");
        if (!id || id->type != Json::Type::String || id->text.empty()) {
            setError(error, "model id must be a non-empty string");
            return false;
        }
        if (!rel || rel->type != Json::Type::String || rel->text.empty()) {
            setError(error, "model path must be a non-empty string");
            return false;
        }
        if (!ids.insert(id->text).second) {
            setError(error, "duplicate model id: " + id->text);
            return false;
        }
        std::filesystem::path modelPath(rel->text);
        if (modelPath.is_absolute()) {
            setError(error, "model path must be relative: " + rel->text);
            return false;
        }
        if (isPathTraversal(modelPath)) {
            setError(error, "model path must not traverse upward: " + rel->text);
            return false;
        }
        if (!std::filesystem::exists(manifestDir / modelPath)) {
            setError(error, "model file missing: " + (manifestDir / modelPath).string());
            return false;
        }

        ModelManifestEntry entry;
        entry.id = id->text;
        entry.path = rel->text;
        if (const Json* name = member(item, "name")) {
            if (name->type != Json::Type::String) {
                setError(error, "model name must be a string");
                return false;
            }
            entry.name = name->text;
        }
        if (const Json* scale = member(item, "scale")) {
            if (scale->type != Json::Type::Number || scale->number <= 0.0) {
                setError(error, "model scale must be a positive number");
                return false;
            }
            entry.scale = float(scale->number);
        }
        parsed.models.push_back(std::move(entry));
    }

    out = std::move(parsed);
    return true;
}
