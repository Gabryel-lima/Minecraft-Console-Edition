#pragma once

class Font;

/** A classe base para todos os componentes de interface gráfica. 
 *  que são usados por todas as telas do jogo.
 *  @param blitOffset - a profundidade de renderização para evitar z-fighting. 
 *  Deve ser incrementada para cada componente renderizado, e resetada para 0 no 
 *  início de cada frame.
 *  @param hLine - desenha uma linha horizontal
 *  @param vLine - desenha uma linha vertical
 *  @param fill - desenha um retângulo sólido
 *  @param fillGradient - desenha um retângulo com um gradiente vertical
 *  @param drawCenteredString - desenha uma string centralizada
 *  @param drawString - desenha uma string normal
 *  @param blit - desenha uma parte de uma textura
*/
class GuiComponent {
protected:
    float blitOffset; // profundidade de renderização para evitar z-fighting

protected:
    /** hline função de desenhar uma linha horizontal 
     *  @param x0 - coordenada x inicial
     *  @param x1 - coordenada x final
     *  @param y - coordenada y da linha
     *  @param col - cor da linha (ARGB)
    */
    void hLine(int x0, int x1, int y, int col);
    /** vline função de desenhar uma linha vertical
     *  @param x - coordenada x da linha
     *  @param y0 - coordenada y inicial
     *  @param y1 - coordenada y final
     *  @param col - cor da linha (ARGB)
     */
    void vLine(int x, int y0, int y1, int col);
    /** fill função de desenhar um retângulo sólido
     *  @param x0 - coordenada x inicial
     *  @param y0 - coordenada y inicial
     *  @param x1 - coordenada x final
     *  @param y1 - coordenada y final
     *  @param col - cor do retângulo (ARGB)
     */
    void fill(int x0, int y0, int x1, int y1, int col);
    /** fillGradient função de desenhar um retângulo com um gradiente vertical
     *  @param x0 - coordenada x inicial
     *  @param y0 - coordenada y inicial
     *  @param x1 - coordenada x final
     *  @param y1 - coordenada y final
     *  @param col1 - cor inicial do gradiente (ARGB)
     *  @param col2 - cor final do gradiente (ARGB)
     */
    void fillGradient(int x0, int y0, int x1, int y1, int col1, int col2);

public:
    /** Construtor da classe GuiComponent */
    GuiComponent();  // 4J added
    /** Desenha uma string centralizada 
     *  @param font - fonte a ser usada para desenhar a string
     *  @param str - string a ser desenhada
     *  @param x - coordenada x do centro da string
     *  @param y - coordenada y da string
     *  @param color - cor da string (ARGB)
    */
    void drawCenteredString(Font* font, const std::wstring& str, int x, int y, int color);
    /** Desenha uma string normal
     *  @param font - fonte a ser usada para desenhar a string
     *  @param str - string a ser desenhada
     *  @param x - coordenada x da string
     *  @param y - coordenada y da string
     *  @param color - cor da string (ARGB)
    */
    void drawString(Font* font, const std::wstring& str, int x, int y, int color);
    /** Desenha uma parte de uma textura
     *  @param x - coordenada x da posição onde a textura será desenhada
     *  @param y - coordenada y da posição onde a textura será desenhada
     *  @param sx - coordenada x da textura
     *  @param sy - coordenada y da textura
     *  @param w - largura da textura
     *  @param h - altura da textura
    */
    void blit(int x, int y, int sx, int sy, int w, int h);
};
