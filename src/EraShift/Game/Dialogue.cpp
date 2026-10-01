#include "EraShift/Game/Dialogue.hpp"

#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

namespace {

using Graphics::BlendMode;
using Graphics::Color;
using Graphics::FontStyle;
using Graphics::Renderer2D;
using Graphics::Rect;
using Graphics::TextAlign;
using Graphics::TextStyle;
namespace Palette = Graphics::Palette;

/// Fraction of the view height the panel occupies. Small enough to sit under the
/// player without covering the thing being talked about.
constexpr float kPanelHeightFraction = 0.22f;
constexpr float kPanelWidthFraction = 0.62f;
/// Padding inside the frame, in reference pixels.
constexpr float kPadding = 14.0f;

} // namespace

void drawDialoguePanel(const StateContext& ctx, const DialoguePanel& panel,
                       Graphics::Vec2 playerCentre, float viewHeight,
                       Graphics::UiScale scale)
{
    if (!panel.open() || ctx.renderer == nullptr || ctx.text == nullptr) {
        return;
    }

    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;

    const float viewW = ctx.renderer->viewportRect().w;
    const float panelW = std::min(scale.px(560.0f), viewW * kPanelWidthFraction);
    const float panelH = std::min(scale.px(150.0f), viewHeight * kPanelHeightFraction);
    const float pad = scale.px(kPadding);

    // Bottom-centred, clamped so it can never leave the viewport. A panel that
    // scrolls off the bottom of the screen is a panel the player cannot read.
    const float x = std::clamp(playerCentre.x - panelW * 0.5f, scale.px(8.0f),
                               std::max(scale.px(8.0f), viewW - panelW - scale.px(8.0f)));
    const float y = viewHeight - panelH - scale.px(16.0f);
    const Rect frame{x, y, panelW, panelH};

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(frame, Palette::PanelFill);
    renderer.drawRectOutline(frame, Palette::PanelBorder, 2.0f);

    const float innerX = frame.x + pad;
    const float innerW = frame.w - pad * 2.0f;
    float cursorY = frame.y + pad;

    if (!panel.title.empty()) {
        TextStyle title = {};
        title.pixelSize = scale.font(15.0f);
        title.color     = Palette::Accent;
        title.bold      = true;
        title.shadow    = Palette::Transparent;
        text.drawInRect(renderer, Rect{innerX, cursorY, innerW, scale.px(16.0f)},
                        panel.title, title);
        cursorY += scale.px(19.0f);
    }

    if (!panel.speaker.empty()) {
        TextStyle speaker = {};
        speaker.pixelSize = scale.font(19.0f);
        speaker.color     = Palette::AccentWarm;
        speaker.bold      = true;
        speaker.shadow    = Palette::Transparent;
        text.drawInRect(renderer, Rect{innerX, cursorY, innerW, scale.px(20.0f)},
                        panel.speaker, speaker);
        cursorY += scale.px(23.0f);
    }

    // The message takes whatever vertical space is left, minus the footer row. That
    // is what keeps the text inside the frame: the wrap width is the *inner*
    // width, and the line limit comes from the height that is actually available,
    // so a long message is ellipsised rather than drawn over the border.
    const float footerH = panel.hint.empty() ? 0.0f : scale.px(18.0f);
    const float messageH = std::max(scale.px(16.0f), frame.bottom() - cursorY - pad - footerH);

    TextStyle body = {};
    body.pixelSize = scale.font(17.0f);
    body.color     = Palette::TextPrimary;
    body.shadow    = Color{0, 0, 0, 0xC0};
    body.wrap      = true;
    body.lineSpacing = 2;

    // One line per `lineSpacing + font size`, which is what `TextRenderer` reports
    // for a line at this size. Computing the cap from the space rather than a
    // constant is what makes this correct at every UI scale.
    const float lineH = text.lineHeight(body);
    body.maxLines = std::max(1, static_cast<int>(messageH / std::max(1.0f, lineH)));

    text.drawInRect(renderer, Rect{innerX, cursorY, innerW, messageH}, panel.message, body);

    if (!panel.hint.empty()) {
        TextStyle hint = {};
        hint.pixelSize = scale.font(14.0f);
        hint.color     = Palette::TextDim;
        hint.shadow    = Palette::Transparent;
        hint.align     = TextAlign::Right;
        text.drawInRect(renderer,
                        Rect{innerX, frame.bottom() - pad - scale.px(14.0f), innerW,
                             scale.px(14.0f)},
                        panel.hint, hint);
    }

    renderer.setBlendMode(BlendMode::None);

    // The bitmap font fallback, so a missing TTF face leaves the player able to
    // read the message rather than facing an empty frame.
    if (!text.ready()) {
        FontStyle fallback;
        fallback.scale = 3;
        Graphics::BitmapFont::drawCentered(renderer, frame.center().x, frame.center().y,
                                           panel.message, Palette::TextPrimary, fallback);
    }
}

} // namespace EraShift::Game