#include "waveform/renderers/allshader/waveformrenderer.h"

#include "waveform/widgets/allshader/waveformwidget.h"

namespace allshader {

WaveformRenderer::WaveformRenderer(WaveformWidgetRenderer* widget)
        : ::WaveformRendererAbstract(widget) {
}

void WaveformRenderer::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

} // namespace allshader
