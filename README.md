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

O `Makefile` na raiz envolve todo o fluxo de Meson/Ninja. Num Linux limpo:

```bash
make deps     # pacotes de sistema (Debian/Ubuntu)
make venv     # virtualenv local com Meson >= 1.7
make run      # configura, compila e abre o jogo
```

Rode `make` (ou `make help`) para ver todos os alvos disponíveis, e `make doctor`
para conferir o que falta no ambiente antes de compilar.

Alvos mais usados:

| Comando | O que faz |
| --- | --- |
| `make setup` | Configura o diretório de build (`renderer=gles` por padrão). |
| `make build` | Compila o `Minecraft.Client`. Configura sozinho se ainda não houver build. |
| `make run` | Compila e executa o jogo a partir de `build/Minecraft.Client`. |
| `make run-only` | Executa o binário já compilado, sem recompilar. |
| `make smoke` | Sobe o jogo por alguns segundos e confirma que ele chega ao menu principal. |
| `make debug` / `make release` | Compila com `buildtype=debug` ou `release`. |
| `make gdb` | Executa o jogo sob o `gdb`. |
| `make assets` | Regenera os assets empacotados (`.arc`, `.loc`, `.col`). |
| `make install PREFIX=~/.local` | Instala o jogo com os assets no prefixo escolhido. |
| `make format` / `make format-check` | Aplica ou confere o `clang-format`. |
| `make clean` / `make distclean` | Limpa os objetos ou remove o diretório de build. |

As opções de build são variáveis do `make`, então dá para combiná-las livremente:

```bash
make build BUILDTYPE=release RENDERER=gl3
make setup SETUP_ARGS="-Denable_frame_profiler=true -Docclusion_culling=bfs"
make run ARGS="--some-flag"
```

Para os detalhes de cada opção do Meson, veja [4jcraft/README.md](4jcraft/README.md).
No Linux o caminho verificado é `renderer=gles`; o backend `ui_backend=shiggy` cai
automaticamente para `java` em CPUs sem AVX.

## 🧭 Observações
Esta é uma implementação própria baseada no repositório do [4jcraft](https://github.com/4jcraft/4jcraft).
Os scripts em `scripts/` usam apenas a biblioteca padrão do Python; este arquivo existe principalmente para o tooling de build baseado em Meson.
