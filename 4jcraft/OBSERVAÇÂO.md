# Não sei se isso está realmente efetivo

No arquivo 4jcraft/Minecraft.Client/Platform/Linux/Stubs/winapi_stubs.h tem a seguinte função:

```c
// https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-outputdebugstringa
static inline void OutputDebugStringA(const char* lpOutputString) {
    if (!lpOutputString) return;

#if defined(_DEBUG) || defined(DEBUG) || defined(_DEBUG_MENUS_ENABLED)
    static int s_debugCharCount = 0;
    static constexpr int kDebugCharsBeforeClear = 50;

    s_debugCharCount += (int)strlen(lpOutputString);

    while (s_debugCharCount >= kDebugCharsBeforeClear) {
        if (isatty(fileno(stderr))) {
            // ANSI clear-screen + cursor-home to keep debug spam bounded.
            fputs("\x1b[2J\x1b[H", stderr);
        }
        s_debugCharCount -= kDebugCharsBeforeClear;
    }
#endif

    fputs(lpOutputString, stderr);
}
```

A função `OutputDebugStringA` é uma implementação personalizada que simula o comportamento da função `OutputDebugStringA` do Windows. Ela recebe uma string de saída e a imprime no stderr. Além disso, ela mantém um contador de caracteres para limitar a quantidade de mensagens de depuração exibidas, limpando a tela quando um certo número de caracteres é atingido.
