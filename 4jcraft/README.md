<div align='center'>

# 4JCraft

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![Meson](https://img.shields.io/badge/build-meson-64f?style=for-the-badge&logo=meson&logoColor=white)](https://mesonbuild.com)
[![Ninja](https://img.shields.io/badge/build-ninja-41454A?style=for-the-badge&logo=ninja&logoColor=white)](https://ninja-build.org)
[![Linux](https://img.shields.io/badge/platform-linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org)
[![GLES](https://img.shields.io/badge/rendering-gles-00A8E8?style=for-the-badge)](meson.options)

4JCraft é um projeto C++/Meson para reconstruir e portar a base de Minecraft: Console Edition.
Este README documenta o fluxo de desenvolvimento atualmente verificado no Linux: como preparar o ambiente, compilar, executar a partir do diretório de build e instalar o resultado.

> 🚀 Build verificado no Linux.
> 🎮 Renderer recomendado: `gles`.
> 🛡️ `ui_backend=shiggy` faz fallback automático para `java` em CPUs Linux sem AVX.
> 📦 O build local copia `Common/`, `music/` e `Sound/` para `build/Minecraft.Client/`.

</div>

## 🧩 Visão geral

O projeto é organizado em módulos separados:

- `Minecraft.Client/` contém o executável principal.
- `Minecraft.Assets/` gera e organiza os assets necessários em tempo de execução.
- `4J.Input/`, `4J.Profile/`, `4J.Render/` e `4J.Storage/` fornecem bibliotecas compartilhadas.
- `scripts/` contém utilitários de build, empacotamento e geração de assets.

O build local copia os assets para o diretório de build do cliente para permitir execução direta sem instalação manual. O jogo espera encontrar `Common/`, `music/` e `Sound/` ao lado do executável.

## 🛠️ Requisitos

O caminho de build documentado aqui é Linux. Para compilar localmente, você vai precisar de:

- Meson 1.7 ou mais novo.
- Ninja.
- Um compilador C++ com suporte a C++23.
- Python 3.
- SDL2.
- OpenGL + GLU para `renderer=gl3`, ou GLESv2 para `renderer=gles`.
- zlib.
- GLM.

Se você estiver usando Debian/Ubuntu, um conjunto próximo ao que a CI usa é:

```bash
sudo apt-get update
sudo apt-get install -y build-essential ccache python3 python3-pip ninja-build pkg-config libsdl2-dev libgl-dev libglu1-mesa-dev libpthread-stubs0-dev libglm-dev
python3 -m pip install --user meson
```

Se você quiser usar o caminho com GLES, substitua as dependências de GLU pelo pacote de desenvolvimento do GLESv2 da sua distribuição.

## ⚡ Build rápido com Nix

O repositório inclui um flake em [flake.nix](flake.nix) com um pacote e um dev shell.

```bash
nix develop
nix build
./result/bin/4jcraft
```

O dev shell já vem com ferramentas úteis como clang, lldb, valgrind e ccache.

## 🔧 Build manual com Meson

O projeto usa Meson + Ninja. O arquivo [meson.options](meson.options) define as opções de build e o arquivo [scripts/llvm_native.txt](scripts/llvm_native.txt) mostra a configuração de clang/LLD usada em CI.

Configuração recomendada para Linux local:

```bash
./scripts/setup_build.sh
meson compile -C build -j $(nproc) -v Minecraft.Client
```

O helper acima ativa `ccache` automaticamente quando ele está instalado, exporta `CCACHE_BASEDIR` e `CCACHE_COMPILERCHECK=content`, escolhe `buildtype=debugoptimized` por padrão e usa `unity=on` no build local. Isso mantém o código principal em unity build e evita inflar desnecessariamente o número de unidades de compilação numa árvore recém-configurada.

Se você quiser priorizar granularidade de rebuild ao editar um único `.cpp`, pode sobrescrever isso com `UNITY=subprojects ./scripts/setup_build.sh` ou passar `-Dunity=subprojects` explicitamente.

Se você usar `--native-file=./scripts/llvm_native_ccache.txt` num Linux limpo, o helper tenta instalar automaticamente as ferramentas que faltarem para esse caminho: `build-essential`, `pkg-config`, `ninja`, `ccache`, `clang`, `lld` e as dependências Python necessárias para o Meson.

Se você rodar o helper de novo no mesmo diretório, ele usa `--reconfigure` em vez de `--wipe`, então os objetos já compilados continuam válidos. Use `./scripts/setup_build.sh build --wipe` apenas quando trocar toolchain, launcher de cache ou outra opção estrutural incompatível.

Se você preferir reproduzir a configuração de clang/LLD usada em automação, use:

```bash
./scripts/setup_build.sh build --native-file=./scripts/llvm_native_ccache.txt
meson compile -C build -j $(nproc)
```

Por padrão, o projeto agora gera um build `debugoptimized`. Se quiser depuração total, passe `-Dbuildtype=debug`; para o melhor desempenho em runtime, passe `-Dbuildtype=release`.

Nos ambientes de desenvolvimento e CI, o `ccache` já fica configurado com `CCACHE_BASEDIR` e `CCACHE_COMPILERCHECK=content`, então compilações equivalentes podem ser reaproveitadas entre builds e entre diretórios diferentes quando a linha de compilação é a mesma. Isso não cruza alvos incompatíveis, mas evita recompilar unidade por unidade sem necessidade.

## ▶️ Executar sem instalar

Depois de compilar, execute o cliente a partir do diretório do build do cliente:

```bash
cd build/Minecraft.Client
./Minecraft.Client
```

Isso funciona porque o build copia os assets necessários para esse diretório. Se o jogo reclamar de arquivos ausentes, confira se você está executando a partir de `build/Minecraft.Client` e se o build terminou sem erros.

## 📦 Instalação

Para instalar em um prefixo local, use um `prefix` explícito ao configurar o build e depois rode `meson install`:

```bash
./scripts/setup_build.sh build --prefix=$HOME/.local
meson compile -C build -j $(nproc)
meson install -C build
```

O executável instalado precisa continuar junto dos diretórios de assets esperados pelo runtime. Se você estiver empacotando o projeto, use `meson install` com o staging root apropriado e mantenha a mesma disposição de arquivos.

Se você estiver usando Nix, `nix build` já produz um pacote com wrapper em `result/bin/4jcraft`.

## 🧪 Opções úteis do Meson

A tabela abaixo resume as opções de build mais usadas, com comandos prontos usando o helper local. Você pode combinar várias flags no mesmo comando; o helper repassa tudo para o Meson e reaproveita o build existente com `--reconfigure` quando possível.

| Opção | Comando | Observações |
| --- | --- | --- |
| `renderer=gles` | `./scripts/setup_build.sh build -Drenderer=gles` | Caminho Linux mais previsível; não exige GLU. |
| `renderer=gl3` | `./scripts/setup_build.sh build -Drenderer=gl3` | Usa OpenGL desktop e exige GLU. |
| `ui_backend=shiggy` | `./scripts/setup_build.sh build -Dui_backend=shiggy` | Backend padrão; no Linux sem AVX faz fallback automático para `java`. |
| `ui_backend=java` | `./scripts/setup_build.sh build -Dui_backend=java` | Útil em máquinas sem AVX ou para evitar o backend `shiggy`. |
| `classic_panorama=true` | `./scripts/setup_build.sh build -Dui_backend=java -Dclassic_panorama=true` | Só vale com `ui_backend=java`. |
| `enable_vsync=true` | `./scripts/setup_build.sh build -Denable_vsync=true` | Mantém o V-Sync ligado. |
| `enable_vsync=false` | `./scripts/setup_build.sh build -Denable_vsync=false` | Desliga o V-Sync e libera as opções de framerate. |
| `enable_frame_profiler=true` | `./scripts/setup_build.sh build -Denable_frame_profiler=true` | Habilita o profiler interno de frame. |
| `occlusion_culling=off` | `./scripts/setup_build.sh build -Docclusion_culling=off` | Desativa todo o culling; útil apenas para debug. |
| `occlusion_culling=frustum` | `./scripts/setup_build.sh build -Docclusion_culling=frustum` | Padrão atual. |
| `occlusion_culling=bfs` | `./scripts/setup_build.sh build -Docclusion_culling=bfs` | Culling por conectividade experimental. |
| `occlusion_culling=hardware` | `./scripts/setup_build.sh build -Docclusion_culling=hardware` | Usa queries de GPU. |
| `buildtype=debug` | `./scripts/setup_build.sh build -Dbuildtype=debug` | Melhor para iterar com menos custo de compilação. |
| `buildtype=debugoptimized` | `./scripts/setup_build.sh build` | Padrão do helper atual. |
| `buildtype=release` | `./scripts/setup_build.sh build -Dbuildtype=release` | Melhor para runtime, mas compila mais devagar. |
| `unity=on` | `./scripts/setup_build.sh build -Dunity=on` | Agrupa fontes em unidades de unity; padrão do helper. |
| `unity=subprojects` | `UNITY=subprojects ./scripts/setup_build.sh build` | Reduz o raio de rebuild ao editar um único `.cpp`. |
| `--native-file=./scripts/llvm_native_ccache.txt` | `./scripts/setup_build.sh build --native-file=./scripts/llvm_native_ccache.txt` | Usa clang + lld + ccache. |
| `--wipe` | `./scripts/setup_build.sh build --wipe` | Recria o diretório de build; use só ao trocar toolchain ou flags estruturais. |

Se você quiser combinar várias opções, basta colocá-las no mesmo comando. Exemplo:

```bash
./scripts/setup_build.sh build -Drenderer=gles -Dbuildtype=debug -Dunity=subprojects
```

Um comando rápido para maior velocidade de compilação durante desenvolvimento é:

```bash
./4jcraft/scripts/setup_build.sh build -Dbuildtype=debug && cd 4jcraft/ && meson setup build --wipe -Drenderer=gles && meson compile -C build -j $(nproc) -v Minecraft.Client
```

O caminho Linux mais previsível continua sendo `renderer=gles`, e o modo `gl3` segue disponível se você tiver as dependências correspondentes instaladas.

## 🪙 Assets

Os assets mínimos e a árvore esperada em runtime estão documentados em [Minecraft.Assets/README.md](Minecraft.Assets/README.md). Se você estiver alterando assets ou investigando falhas de carregamento, esse arquivo é o melhor ponto de referência.

## 🤝 Contribuição

Antes de enviar mudanças, siga o padrão dos módulos, preserve os comentários `4jcraft:` quando eles forem necessários e mantenha os patches focados.

## 🧯 Problemas comuns

- Se você estiver configurando `renderer=gl3`, instale as dependências de GLU; para o caminho Linux verificado, use `-Drenderer=gles`.
- Se uma máquina Linux antiga não suportar AVX, mantenha `ui_backend=shiggy` ou deixe o padrão: o build agora troca automaticamente para `java` e evita o crash de instrução ilegal.
- Se o jogo iniciar mas não encontrar assets, execute a partir de `build/Minecraft.Client` ou use a instalação empacotada.
- Se você quiser reproduzir o ambiente de CI, consulte os workflows em [.github/workflows/build-linux.yml](.github/workflows/build-linux.yml) e [.github/workflows/release-linux.yml](.github/workflows/release-linux.yml).
