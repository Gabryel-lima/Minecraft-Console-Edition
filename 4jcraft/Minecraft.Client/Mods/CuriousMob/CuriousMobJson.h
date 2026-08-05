#pragma once

// Helpers de JSON do CuriousMob.
//
// Leitura: o schema das ações é um objeto PLANO (sem objetos/arrays aninhados)
// e fixo — ver mods/CuriousMob/protocol/messages.md. Por isso a leitura é
// feita por extração de campo, não por um parser completo: é o suficiente e
// evita puxar uma dependência de terceiros para dentro do jogo.
//
// Escrita: o estado, ao contrário, TEM estrutura aninhada (alvo, inventário,
// container). JsonWriter é um serializador de escrita sequencial mínimo que
// cuida das vírgulas e do escape de string para não termos concatenação de
// `std::ostringstream` espalhada pelo controller.

#include <sstream>
#include <string>

namespace cmjson {

// --- Leitura (objeto plano) --------------------------------------------

// Todas retornam `fallback` se o campo não existir ou não fizer parse.
std::string getString(const std::string& json, const std::string& key,
                      const std::string& fallback = "");
bool getBool(const std::string& json, const std::string& key,
             bool fallback = false);
int getInt(const std::string& json, const std::string& key, int fallback = 0);
float getFloat(const std::string& json, const std::string& key,
               float fallback = 0.0f);
bool hasKey(const std::string& json, const std::string& key);

// --- Escrita -----------------------------------------------------------

class JsonWriter {
public:
    JsonWriter();

    // Objetos e arrays. `key` vazio = elemento de array (sem nome).
    JsonWriter& beginObject(const std::string& key = "");
    JsonWriter& endObject();
    JsonWriter& beginArray(const std::string& key);
    JsonWriter& endArray();

    JsonWriter& field(const std::string& key, const std::string& value);
    JsonWriter& field(const std::string& key, const std::wstring& value);
    JsonWriter& field(const std::string& key, const char* value);
    JsonWriter& field(const std::string& key, bool value);
    JsonWriter& field(const std::string& key, int value);
    JsonWriter& field(const std::string& key, long long value);
    JsonWriter& field(const std::string& key, double value);
    JsonWriter& nullField(const std::string& key);

    std::string str() const;

private:
    std::ostringstream out;
    bool needComma;

    void prefix(const std::string& key);
};

// Converte uma wstring do jogo (nomes de bioma/entidade) para UTF-8.
std::string toUtf8(const std::wstring& value);

}  // namespace cmjson
