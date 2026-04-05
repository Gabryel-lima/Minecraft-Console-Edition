#pragma once

class IntBuffer;
class Options;
class Textures;
class ResourceLocation;

/** Classe que representa uma fonte de texto. 
 *  @param charWidths - array de larguras dos caracteres
 *  @param fontTexture - ID da textura da fonte
 *  @param random - gerador de números aleatórios para efeitos de formatação
 *  @param colors - array de cores para formatação de texto
 *  @param textures - referência para a classe de texturas para carregar a textura da fonte
 *  @param xPos - posição x atual para renderização de texto
 *  @param yPos - posição y atual para renderização de texto
 *  @param enforceUnicodeSheet - flag para forçar o uso da folha de caracteres unicode
 *  @param bidirectional - flag para habilitar a renderização de texto bidirecional
 *  @param m_cols - número de colunas na folha de caracteres
 *  @param m_rows - número de linhas na folha de caracteres
 *  @param m_charWidth - largura máxima de um caractere
 *  @param m_charHeight - altura máxima de um caractere
 *  @param m_textureLocation - localização da textura da fonte
 *  @param m_charMap - mapa de caracteres para mapeamento de caracteres personalizados
*/
class Font {
private:
    int* charWidths; // array de larguras dos caracteres

public:
    int fontTexture; // ID da textura da fonte
    Random* random;  // gerador de números aleatórios para efeitos de formatação

private:
    int colors[32];  // RGB colors for formatting

    Textures* textures; // referência para a classe de texturas para carregar a textura da fonte

    float xPos; // posição x atual para renderização de texto
    float yPos; // posição y atual para renderização de texto

    bool enforceUnicodeSheet;  // use unicode sheet for ascii
    bool bidirectional;        // use bidi to flip strings

    int m_cols;                           // Number of columns in font sheet
    int m_rows;                           // Number of rows in font sheet
    int m_charWidth;                      // Maximum character width
    int m_charHeight;                     // Maximum character height
    ResourceLocation* m_textureLocation;  // Texture
    std::map<int, int> m_charMap;         // Map of character code to font sheet index

public:
    /** Construtor da classe Font.
     *  @param options - opções de configuração
     *  @param name - nome da fonte
     *  @param textures - referência para a classe de texturas
     *  @param enforceUnicode - flag para forçar o uso da folha de caracteres unicode
     *  @param textureLocation - localização da textura da fonte
     *  @param cols - número de colunas na folha de caracteres
     *  @param rows - número de linhas na folha de caracteres
     *  @param charWidth - largura máxima de um caractere
     *  @param charHeight - altura máxima de um caractere
     *  @param charMap - mapa de caracteres para mapeamento de caracteres personalizados
     */
    Font(Options* options, const std::wstring& name, Textures* textures,
         bool enforceUnicode, ResourceLocation* textureLocation, int cols,
         int rows, int charWidth, int charHeight,
         unsigned short charMap[] = nullptr);
    // 4J Stu - This dtor clashes with one in xui! We never delete these anyway
    // so take it out for now. Can go back when we have got rid of XUI
    ~Font();
    void renderFakeCB(IntBuffer* cb);  // 4J added

private:
    /** Renders a single character.
     *  @param c - character to render
     */
    void renderCharacter(wchar_t c);  // 4J added

public:
     /** Draws a shadowed string.
     *  @param str - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param color - color of the text
     */
    void drawShadow(const std::wstring& str, int x, int y, int color);
    /** Draws a shadowed string with word wrap.
     *  @param str - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param w - width for word wrap
     *  @param color - color of the text
     *  @param h - height for word wrap
     */
    void drawShadowWordWrap(const std::wstring& str, int x, int y, int w,
                            int color, int h);  // 4J Added h param
    /** Draws a string.
     *  @param str - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param color - color of the text
     */
    void draw(const std::wstring& str, int x, int y, int color);

private:
    /** Reorders the string according to bidirectional levels. A bit expensive at
     *  the moment.
     *  @param str - string to reorder
     *  @return reordered string
     */
    std::wstring reorderBidi(const std::wstring& str);

    /** Draws a string with optional drop shadow.
     *  @param str - string to draw
     *  @param dropShadow - flag to enable drop shadow
     */
    void draw(const std::wstring& str, bool dropShadow);
    /** Draws a string with optional drop shadow and color.
     *  @param str - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param color - color of the text
     *  @param dropShadow - flag to enable drop shadow
     */
    void draw(const std::wstring& str, int x, int y, int color,
              bool dropShadow);
    /** Maps a character to its corresponding index in the font texture.
     *  @param c - character to map
     *  @return index of the character in the font texture
     */
    int MapCharacter(wchar_t c);      // 4J added
    /** Checks if a character exists in the font texture.
     *  @param c - character to check
     *  @return true if the character exists, false otherwise
     */
    bool CharacterExists(wchar_t c);  // 4J added

public:
    /** Returns the width of a string.
     *  @param str - string to measure
     *  @return width of the string
     */
    int width(const std::wstring& str);
    /** Sanitizes a string by removing invalid characters.
     *  @param str - string to sanitize
     *  @return sanitized string
     */
    std::wstring sanitize(const std::wstring& str);
    /** Draws a word-wrapped string.
     *  @param string - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param w - width for word wrap
     *  @param col - color of the text
     *  @param h - height for word wrap
     */
    void drawWordWrap(const std::wstring& string, int x, int y, int w, int col,
                      int h);  // 4J Added h param

private:
    /** Draws a word-wrapped string internally.
     *  @param string - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param w - width for word wrap
     *  @param col - color of the text
     *  @param h - height for word wrap
     */
    void drawWordWrapInternal(const std::wstring& string, int x, int y, int w,
                              int col, int h);  // 4J Added h param

public:
    /** Draws a word-wrapped string with optional darkening.
     *  @param string - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param w - width for word wrap
     *  @param col - color of the text
     *  @param darken - flag to enable darkening
     *  @param h - height for word wrap
     */
    void drawWordWrap(const std::wstring& string, int x, int y, int w, int col,
                      bool darken, int h);  // 4J Added h param

private:
    /** Draws a word-wrapped string with optional darkening internally.
     *  @param string - string to draw
     *  @param x - x position
     *  @param y - y position
     *  @param w - width for word wrap
     *  @param col - color of the text
     *  @param darken - flag to enable darkening
     *  @param h - height for word wrap
     */
    void drawWordWrapInternal(const std::wstring& string, int x, int y, int w,
                              int col, bool darken, int h);  // 4J Added h param

public:
    /** Returns the height of a word-wrapped string.
     *  @param string - string to measure
     *  @param w - width for word wrap
     *  @return height of the word-wrapped string
     */
    int wordWrapHeight(const std::wstring& string, int w);
    /** Sets whether to enforce the use of the Unicode font sheet.
     *  @param enforceUnicodeSheet - flag to enable or disable enforcement
     */
    void setEnforceUnicodeSheet(bool enforceUnicodeSheet);
    /** Sets whether to enable bidirectional text rendering.
     *  @param bidirectional - flag to enable or disable bidirectional text
     */
    void setBidirectional(bool bidirectional);

    // 4J-PB - check for invalid player name - Japanese local name
    bool AllCharactersValid(const std::wstring& str);
};
