// Harness de teste do CuriousMobJson (ver tests/test_cpp_json.py).
//
// O serializador/leitor JSON do lado C++ é escrito à mão, e um erro nele é
// invisível na compilação do jogo: ele só aparece como um agente que ignora
// silenciosamente um campo de ação, ou como um `json.loads` estourando no
// Python no meio de uma sessão. Este harness compila apenas
// CuriousMobJson.cpp (sem nenhuma dependência do motor) e:
//
//   modo "write"  -> imprime um estado com a mesma FORMA que
//                    CuriousMobController::buildStateJson produz
//                    (aninhamento, arrays, null, wstring, escapes), para o
//                    teste em Python validar com json.loads;
//   modo "read"   -> lê uma linha de ação JSON no stdin e imprime os campos
//                    extraídos em `chave=valor`, para o teste conferir
//                    contra o que o Action.to_json() do Python gerou.

#include <cstdio>
#include <iostream>
#include <string>

#include "CuriousMobJson.h"

using cmjson::JsonWriter;

namespace {

void writeSampleState() {
    JsonWriter w;
    w.field("tick", 500LL)
        .field("x", 12.5)
        .field("y", -64.25)
        .field("on_ground", true)
        .field("alive", false)
        // Valores que quebrariam o json.loads do Python se passassem cru.
        .field("nan_guard", 0.0 / 0.0)
        .field("inf_guard", 1.0 / 0.0);

    w.beginObject("biome")
        .field("id", 4)
        .field("name", std::wstring(L"Floresta de Ação"))
        .endObject();

    w.beginObject("target").field("type", "tile").field("face", 1).endObject();
    w.nullField("container");
    w.nullField("nearest_entity");

    w.beginObject("inventory").field("selected", 2);
    w.beginArray("items");
    for (int i = 0; i < 3; i++) {
        w.beginObject()
            .field("slot", i)
            .field("id", 264 + i)
            .field("count", 1)
            .field("aux", 0)
            .endObject();
    }
    w.endArray();
    w.beginArray("armor");  // array vazio: caso de borda das vírgulas
    w.endArray();
    w.endObject();

    w.field("quoted", std::string("aspas:\" barra:\\ nova-linha:\n tab:\t"));

    printf("%s\n", w.str().c_str());
}

void readAction(const std::string& json) {
    printf("move=%s\n", cmjson::getString(json, "move", "<ausente>").c_str());
    printf("turn_str=%s\n", cmjson::getString(json, "turn", "<ausente>").c_str());
    printf("turn_num=%.4f\n", cmjson::getFloat(json, "turn"));
    printf("look_pitch=%.4f\n", cmjson::getFloat(json, "look_pitch"));
    printf("forward=%.4f\n", cmjson::getFloat(json, "forward", -99.0f));
    printf("strafe=%.4f\n", cmjson::getFloat(json, "strafe", -99.0f));
    printf("jump=%d\n", static_cast<int>(cmjson::getBool(json, "jump")));
    printf("sneak=%d\n", static_cast<int>(cmjson::getBool(json, "sneak")));
    printf("sprint=%d\n", static_cast<int>(cmjson::getBool(json, "sprint")));
    printf("attack=%d\n", static_cast<int>(cmjson::getBool(json, "attack")));
    printf("use=%d\n", static_cast<int>(cmjson::getBool(json, "use")));
    printf("break_block=%d\n", static_cast<int>(cmjson::getBool(json, "break_block")));
    printf("place_block=%d\n", static_cast<int>(cmjson::getBool(json, "place_block")));
    printf("drop=%d\n", static_cast<int>(cmjson::getBool(json, "drop")));
    printf("drop_stack=%d\n", static_cast<int>(cmjson::getBool(json, "drop_stack")));
    printf("drop_all=%d\n", static_cast<int>(cmjson::getBool(json, "drop_all")));
    printf("release_item=%d\n", static_cast<int>(cmjson::getBool(json, "release_item")));
    printf("stop_item=%d\n", static_cast<int>(cmjson::getBool(json, "stop_item")));
    printf("close_container=%d\n",
           static_cast<int>(cmjson::getBool(json, "close_container")));
    printf("select_slot=%d\n", cmjson::getInt(json, "select_slot", -1));
    printf("swap_from=%d\n", cmjson::getInt(json, "swap_from", -1));
    printf("swap_to=%d\n", cmjson::getInt(json, "swap_to", -1));
    printf("has_select_slot=%d\n",
           static_cast<int>(cmjson::hasKey(json, "select_slot")));
    printf("has_forward=%d\n", static_cast<int>(cmjson::hasKey(json, "forward")));
    printf("has_inexistente=%d\n",
           static_cast<int>(cmjson::hasKey(json, "inexistente")));
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "write";
    if (mode == "write") {
        writeSampleState();
        return 0;
    }
    if (mode == "read") {
        std::string line;
        std::getline(std::cin, line);
        readAction(line);
        return 0;
    }
    fprintf(stderr, "uso: json_harness [write|read]\n");
    return 2;
}
