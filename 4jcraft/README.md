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
- OpenGL ou GLESv2, dependendo do renderer escolhido.
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
meson setup build --wipe -Drenderer=gles
meson compile -C build -j $(nproc) -v Minecraft.Client
```

Se você preferir reproduzir a configuração de clang/LLD usada em automação, use:

```bash
meson setup build --wipe --native-file=./scripts/llvm_native.txt
meson compile -C build -j $(nproc)
```

Por padrão, o projeto gera um build de debug. Se quiser outro tipo de build, passe `-Dbuildtype=release` ou `-Dbuildtype=debugoptimized` ao `meson setup`.

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
meson setup build --wipe --prefix=$HOME/.local -Drenderer=gles
meson compile -C build -j $(nproc)
meson install -C build
```

O executável instalado precisa continuar junto dos diretórios de assets esperados pelo runtime. Se você estiver empacotando o projeto, use `meson install` com o staging root apropriado e mantenha a mesma disposição de arquivos.

Se você estiver usando Nix, `nix build` já produz um pacote com wrapper em `result/bin/4jcraft`.

## 🧪 Opções úteis do Meson

As opções disponíveis hoje são estas:

- `renderer=gl3|gles` - escolhe o backend de renderização.
- `ui_backend=shiggy|java` - escolhe a implementação da UI. Em Linux, `shiggy` usa fallback para `java` quando a CPU não anuncia AVX.
- `classic_panorama=true|false` - habilita o panorama clássico, somente com `ui_backend=java`.
- `enable_vsync=true|false` - controla V-Sync.
- `enable_frame_profiler=true|false` - habilita o profiler de frame.
- `occlusion_culling=off|frustum|bfs|hardware` - escolhe o modo de occlusion culling.

O caminho Linux mais previsível hoje é `renderer=gles`. O modo padrão `gl3` continua disponível se você tiver as dependências correspondentes instaladas.

## 🪙 Assets

Os assets mínimos e a árvore esperada em runtime estão documentados em [Minecraft.Assets/README.md](Minecraft.Assets/README.md). Se você estiver alterando assets ou investigando falhas de carregamento, esse arquivo é o melhor ponto de referência.

## 🤝 Contribuição

Antes de enviar mudanças, siga o padrão dos módulos, preserve os comentários `4jcraft:` quando eles forem necessários e mantenha os patches focados.

## 🧯 Problemas comuns

- Se a configuração falhar por causa de GLU, use `-Drenderer=gles` ou instale as dependências de GLU para o backend `gl3`.
- Se uma máquina Linux antiga não suportar AVX, mantenha `ui_backend=shiggy` ou deixe o padrão: o build agora troca automaticamente para `java` e evita o crash de instrução ilegal.
- Se o jogo iniciar mas não encontrar assets, execute a partir de `build/Minecraft.Client` ou use a instalação empacotada.
- Se você quiser reproduzir o ambiente de CI, consulte os workflows em [.github/workflows/build-linux.yml](.github/workflows/build-linux.yml) e [.github/workflows/release-linux.yml](.github/workflows/release-linux.yml).
