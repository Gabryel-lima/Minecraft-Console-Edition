<div align='center'>

# Minecraft-Console-Edition

[![Docs](https://img.shields.io/badge/docs-4JCraft-2ea44f?style=for-the-badge)](4jcraft/README.md)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](4jcraft/README.md)
[![Meson](https://img.shields.io/badge/build-meson-64f?style=for-the-badge&logo=meson&logoColor=white)](4jcraft/README.md)
[![Linux](https://img.shields.io/badge/platform-linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](4jcraft/README.md)

Repositório raiz do workspace para a reconstrução de Minecraft: Console Edition.

</div>

## ✨ O que há aqui

- `4jcraft/` contém o projeto principal em C++/Meson.
- `scripts/` reúne utilitários Python de apoio.
- `requirements.txt` lista as dependências Python usadas pelo fluxo de build.

## 🚀 Começo rápido

1. Leia [4jcraft/README.md](4jcraft/README.md).
2. No Linux, siga o caminho documentado ali com `renderer=gles` se quiser o build verificado. O backend `ui_backend=shiggy` agora cai automaticamente para `java` em CPUs sem AVX.
3. Para preparar o ambiente Python de apoio, rode `python3 -m pip install -r requirements.txt`.

## 🧭 Observações
Esta é uma implementação própria baseada no repositório do [4jcraft](https://github.com/4jcraft/4jcraft).
Os scripts em `scripts/` usam apenas a biblioteca padrão do Python; este arquivo existe principalmente para o tooling de build baseado em Meson.
