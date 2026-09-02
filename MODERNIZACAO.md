# Estudo de modernização (Console Edition → paridade com Bedrock atual)

Este documento é **separado de propósito** do `TODOS.md`. O `TODOS.md` é o diário
de depuração do agente/bot (`CuriousMob`, `BotPlayer`) — uma frente de trabalho
sobre IA/automação dentro do jogo. Este arquivo aqui é outra frente, sem relação
direta: trazer, aos poucos e como estudo, mecânicas que o Minecraft Bedrock tem
hoje e que esta reconstrução da Console Edition não tem (o código deste projeto
reflete uma paridade antiga, próxima da época do Java ~1.11).

Se algum dia as duas frentes se cruzarem (ex.: o bot precisar reagir a uma
mecânica nova implementada aqui), isso deve ser anotado nos dois arquivos, mas
o trabalho de portar mecânica continua registrado neste documento.

## Objetivo

Aprender como o jogo foi evoluindo, mecânica por mecânica, reimplementando o
*comportamento observado* dentro da arquitetura C++ que já existe em `4jcraft/`.
Não é "atualizar" no sentido de aplicar um patch oficial — é engenharia reversa
por caixa-preta, mecânica a mecânica, com código próprio.

## Regras invioláveis (o motivo de este documento existir)

1. **Nunca commitar assets do jogo oficial.** Texturas, sons, música, modelos,
   fontes, qualquer arquivo binário extraído de uma cópia (comprada ou não) do
   Bedrock/Java não entra neste repositório. Se uma feature precisa de asset
   visual/sonoro novo, usamos placeholder próprio ou recurso livre/licenciado,
   nunca o arquivo original da Mojang/Microsoft.
2. **Nunca copiar ou traduzir literalmente código-fonte de terceiros**
   (decompilado, vazado ou de outro projeto com licença incompatível). O que se
   porta é o *comportamento observado em jogo* (regras, fórmulas, timings),
   descrito em texto antes de virar código. Código é sempre escrito do zero.
3. **Toda feature começa com uma especificação escrita**, não direto no
   editor. Ver seção "Fluxo de trabalho por mecânica" abaixo — isso é o que
   garante que o que entra no repo é a *regra do jogo* (não protegida por
   copyright) e não a *expressão* de alguém (protegida).
4. **Se a origem do 4jcraft upstream for esclarecida como derivada de
   descompilação de binário** (ainda não verificado — ver nota abaixo), isso
   deve ser documentado aqui antes de qualquer novo trabalho em cima do código
   legado, para decidir se algo precisa ser reescrito.

> **Nota em aberto, não verificada:** nomes de classes já existentes no projeto
> (`ServerLevel`, `EntityTracker`, `TrackedEntity`, `PlayerList`, `RemotePlayer`)
> são específicos demais para terem sido descobertos só jogando. Isso é uma
> hipótese, não um fato confirmado — ninguém neste projeto investigou a
> proveniência exata do 4jcraft upstream ainda. Não é bloqueante para o
> trabalho *novo* descrito aqui (que segue as regras acima de qualquer forma),
> mas é algo a esclarecer antes de tratar o código legado como modelo a seguir.

## Fluxo de trabalho por mecânica

Cada mecânica nova vira uma entrada na seção "Backlog" abaixo e segue este
processo:

1. **Observação** — jogar a versão atual (Bedrock), anotar o comportamento em
   termos de regra: gatilhos, condições, valores numéricos, timing. Sem
   capturar tela do jogo pra usar como asset, sem extrair arquivo nenhum.
2. **Especificação** — escrever a regra em texto simples (pseudo-código ou
   descrição), como uma entrada nova neste documento. Essa etapa é o que prova
   que estamos portando comportamento, não copiando implementação.
3. **Mapeamento na arquitetura atual** — identificar em `4jcraft/` onde a
   mecânica se encaixa (que classes, que sistemas já existem que podem ser
   reaproveitados ou servem de referência de padrão de código).
4. **Implementação** — código C++ novo, seguindo o padrão do projeto
   (`make format` antes de commitar).
5. **Validação** — testar no jogo (`make run` / `make smoke`), comparar contra
   a especificação do passo 2.
6. **Atualizar este documento** — mover a entrada do Backlog para "Concluído",
   com link do commit.

## Backlog (mecânicas candidatas, ainda não especificadas)

> Preencher aqui conforme forem escolhidas. Uma linha por mecânica, com
> prioridade e status. Exemplo de formato:

| Mecânica | Prioridade | Status |
| --- | --- | --- |
| _(nenhuma escolhida ainda — ver "Próximo passo")_ | — | — |

## Concluído

_(vazio por enquanto)_

## Próximo passo

Escolher a primeira mecânica concreta para passar pelo fluxo acima. Boas
candidatas para começar (pequenas, isoladas, fáceis de observar e validar):
sistema de fome/exaustão atualizado, alguma regra de crafting específica, ou
um comportamento de mob simples. Preencher a tabela de Backlog acima assim que
a primeira for escolhida.
