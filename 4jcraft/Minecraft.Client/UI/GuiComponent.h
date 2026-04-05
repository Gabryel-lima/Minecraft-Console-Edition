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
    float blitOffset;

protected:
    void hLine(int x0, int x1, int y, int col);
    void vLine(int x, int y0, int y1, int col);
    void fill(int x0, int y0, int x1, int y1, int col);
    void fillGradient(int x0, int y0, int x1, int y1, int col1, int col2);

public:
    GuiComponent();  // 4J added
    void drawCenteredString(Font* font, const std::wstring& str, int x, int y,
                            int color);
    void drawString(Font* font, const std::wstring& str, int x, int y,
                    int color);
    void blit(int x, int y, int sx, int sy, int w, int h);
};
