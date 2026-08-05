#include "CuriousMobJson.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>

namespace cmjson {
namespace {

// Devolve a posição logo após os dois-pontos do campo `key`, ou npos.
//
// Exige que a ocorrência de `"key"` seja seguida de ':' — sem isso, procurar
// pelo campo `forward` numa ação `{"move": "forward", "turn": -12.5}`
// casaria com o VALOR "forward" e leria o -12.5 do campo seguinte. O par
// move/forward do nosso protocolo cai exatamente nesse caso.
size_t valueStart(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t search = 0;
    while (true) {
        size_t pos = json.find(needle, search);
        if (pos == std::string::npos) return std::string::npos;

        size_t after = pos + needle.size();
        while (after < json.size() &&
               (json[after] == ' ' || json[after] == '\t')) {
            after++;
        }
        if (after >= json.size() || json[after] != ':') {
            search = pos + 1;  // era um valor, não uma chave: segue procurando
            continue;
        }

        after++;  // pula o ':'
        while (after < json.size() &&
               (json[after] == ' ' || json[after] == '\t')) {
            after++;
        }
        return after < json.size() ? after : std::string::npos;
    }
}

// Fim do valor escalar iniciado em `start` (primeira vírgula/fecha-chaves).
size_t valueEnd(const std::string& json, size_t start) {
    size_t end = json.size();
    for (size_t i = start; i < json.size(); i++) {
        if (json[i] == ',' || json[i] == '}' || json[i] == ']') {
            end = i;
            break;
        }
    }
    return end;
}

}  // namespace

bool hasKey(const std::string& json, const std::string& key) {
    return valueStart(json, key) != std::string::npos;
}

std::string getString(const std::string& json, const std::string& key,
                      const std::string& fallback) {
    size_t start = valueStart(json, key);
    if (start == std::string::npos || json[start] != '"') return fallback;
    size_t close = json.find('"', start + 1);
    if (close == std::string::npos) return fallback;
    return json.substr(start + 1, close - start - 1);
}

bool getBool(const std::string& json, const std::string& key, bool fallback) {
    size_t start = valueStart(json, key);
    if (start == std::string::npos) return fallback;
    if (json.compare(start, 4, "true") == 0) return true;
    if (json.compare(start, 5, "false") == 0) return false;
    // Aceita 0/1 numérico também: o lado Python pode serializar flags de um
    // espaço de ação discreto como int sem que isso seja um erro.
    if (json[start] == '0') return false;
    if (json[start] >= '1' && json[start] <= '9') return true;
    return fallback;
}

int getInt(const std::string& json, const std::string& key, int fallback) {
    size_t start = valueStart(json, key);
    if (start == std::string::npos || json[start] == '"') return fallback;
    std::string raw = json.substr(start, valueEnd(json, start) - start);
    try {
        // stod, não stoi: o Python pode mandar 3.0 onde esperamos um slot, e
        // stoi("3.0") pararia no ponto (ok) mas stoi("3e0") lançaria.
        return static_cast<int>(std::stod(raw));
    } catch (...) {
        return fallback;
    }
}

float getFloat(const std::string& json, const std::string& key,
               float fallback) {
    size_t start = valueStart(json, key);
    if (start == std::string::npos || json[start] == '"') return fallback;
    std::string raw = json.substr(start, valueEnd(json, start) - start);
    try {
        return std::stof(raw);
    } catch (...) {
        return fallback;
    }
}

// --- JsonWriter --------------------------------------------------------

JsonWriter::JsonWriter() : needComma(false) {
    // Precisão fixa e sem notação científica: o `json` do Python aceita
    // ambos, mas números como 1e+06 num campo de posição são ilegíveis nos
    // logs de depuração da ponte.
    out << std::fixed << std::setprecision(3);
    out << "{";
}

void JsonWriter::prefix(const std::string& key) {
    if (needComma) out << ",";
    if (!key.empty()) out << "\"" << key << "\":";
    needComma = true;
}

JsonWriter& JsonWriter::beginObject(const std::string& key) {
    prefix(key);
    out << "{";
    needComma = false;
    return *this;
}

JsonWriter& JsonWriter::endObject() {
    out << "}";
    needComma = true;
    return *this;
}

JsonWriter& JsonWriter::beginArray(const std::string& key) {
    prefix(key);
    out << "[";
    needComma = false;
    return *this;
}

JsonWriter& JsonWriter::endArray() {
    out << "]";
    needComma = true;
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& key,
                              const std::string& value) {
    prefix(key);
    out << "\"";
    for (char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "?";  // controle: descarta, não vale escapar
                } else {
                    out << c;
                }
        }
    }
    out << "\"";
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& key,
                              const std::wstring& value) {
    return field(key, toUtf8(value));
}

JsonWriter& JsonWriter::field(const std::string& key, const char* value) {
    return field(key, std::string(value));
}

JsonWriter& JsonWriter::field(const std::string& key, bool value) {
    prefix(key);
    out << (value ? "true" : "false");
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& key, int value) {
    prefix(key);
    out << value;
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& key, long long value) {
    prefix(key);
    out << value;
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& key, double value) {
    prefix(key);
    // NaN/inf não são JSON válido e quebrariam o json.loads do Python — o
    // motor produz NaN em casos de borda (ex. saturação após respawn).
    if (std::isnan(value) || std::isinf(value)) {
        out << "0.0";
    } else {
        out << value;
    }
    return *this;
}

JsonWriter& JsonWriter::nullField(const std::string& key) {
    prefix(key);
    out << "null";
    return *this;
}

std::string JsonWriter::str() const { return out.str() + "}"; }

std::string toUtf8(const std::wstring& value) {
    // Codificação UTF-8 manual a partir de code points. Não usamos
    // std::wstring_convert (deprecado em C++17) nem std::locale (depende de
    // locale instalado no sistema, o que tornaria a saída não determinística).
    std::string result;
    result.reserve(value.size());
    for (wchar_t wc : value) {
        unsigned int cp = static_cast<unsigned int>(wc);
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

}  // namespace cmjson
