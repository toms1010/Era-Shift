#include "EraShift/Game/states/PausedState.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>

namespace EraShift::Game {

using Core::GameState;
using StateContext = ::EraShift::StateContext;
using Graphics::BitmapFont;
using Graphics::BlendMode;
using Graphics::Color;
using Graphics::TextAlign;
using Graphics::TextStyle;
using Graphics::TextVAlign;
using Graphics::UiScale;
namespace Palette = Graphics::Palette;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::Vec2;
using Input::Action;

namespace {

Rect fullArea(Renderer2D& renderer)
{
    return Rect{0.0f, 0.0f,
                static_cast<float>(renderer.camera().viewportWidth()),
                static_cast<float>(renderer.camera().viewportHeight())};
}

std::string onOff(bool value)
{
    return value ? "ON" : "OFF";
}

std::string percent(int value)
{
    return std::to_string(value) + "%";
}

} // namespace

void PausedState::syncScale(StateContext& ctx)
{
    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles        = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }
}

void SettingsState::syncScale(StateContext& ctx)
{
    const UiScale latest = uiScaleFor(ctx);
    if (latest.factor != m_viewportScale.factor) {
        m_viewportScale = latest;
        m_styles        = MenuStyles::make(latest);
        m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    }
}

// ---------------------------------------------------------------------------
// PausedState
// ---------------------------------------------------------------------------
PausedState::PausedState(GameState id)
    : IGameState(id)
{
}

void PausedState::onEnter(StateContext& ctx)
{
    m_time = 0.0f;
    m_viewportScale = uiScaleFor(ctx);
    m_styles = MenuStyles::make(m_viewportScale);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    m_menu.setItems({
        {"RESUME",       "return to the timeline", true},
        {"SETTINGS",     "options",                 true},
        {"MAIN MENU",    "abandon this run",        true},
        {"QUIT",         "exit to desktop",         true},
    });
    ctx.log->info("Game", "paused");
}

void PausedState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    if (ctx.input == nullptr) {
        return;
    }
    syncScale(ctx);

    // Escape both resumes and confirms, so the player never gets trapped.
    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->pop();
        return;
    }

    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }

    const bool confirm = ctx.input->wasPressed(Action::Interact) || ctx.input->wasPressed(Action::Jump);
    if (!confirm) {
        return;
    }

    if (item->label == "RESUME") {
        ctx.states->pop();
    } else if (item->label == "SETTINGS") {
        ctx.states->push(std::make_shared<SettingsState>());
    } else if (item->label == "MAIN MENU") {
        ctx.states->popToRoot();
    } else if (item->label == "QUIT") {
        ctx.loop->requestStop();
    }
}

void PausedState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const Rect area = fullArea(renderer);
    static_cast<void>(alpha);

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);

    // Dim the frozen frame behind the menu rather than clearing it, so the
    // player keeps their bearings.
    renderer.drawRect(area, Color{0, 0, 0, 0xB0});

    const PanelLayout layout =
        layoutPanel(area, m_styles, m_menu.items().size(), "PAUSED", "ESC to resume", text);

    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRect(layout.panel, Palette::PanelBorder);

    TextStyle heading = m_styles.heading;
    text.drawInRect(renderer, layout.heading, "PAUSED", heading);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);
    text.drawInRect(renderer, layout.footer, "ESC to resume", m_styles.hint);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);
}

// ---------------------------------------------------------------------------
// SettingsState
// ---------------------------------------------------------------------------
SettingsState::SettingsState(GameState id)
    : IGameState(id)
{
}

void SettingsState::onEnter(StateContext& ctx)
{
    m_time = 0.0f;
    m_viewportScale = uiScaleFor(ctx);
    m_styles = MenuStyles::make(m_viewportScale);
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);

    // Read current values out of the config store so the screen reflects what
    // the game is actually using.
    Core::ConfigManager& config = *ctx.config;
    const Core::ConfigStore& store = config.store();
    m_fullscreen    = store.getInt("graphics", "fullscreen", 0);
    m_vsync         = store.getInt("graphics", "vsync", 1);
    m_showFps       = store.getInt("debug", "showStats", 0);
    m_masterVolume  = store.getInt("audio", "masterVolume", 80);
    m_musicVolume   = store.getInt("audio", "musicVolume", 70);
    m_sfxVolume     = store.getInt("audio", "sfxVolume", 90);
    m_uiScale       = store.getInt("graphics", "uiScale", 100);

    buildItems(ctx);
    ctx.log->info("Game", "entered Settings");
}

void SettingsState::onExit(StateContext& ctx)
{
    ctx.config->saveUserConfig(*ctx.log);
    ctx.log->info("Game", "settings saved");
}

void SettingsState::buildItems(StateContext& ctx)
{
    // Row metrics come from the shared style, never from a literal: the pause
    // menu, the settings screen and the main menu must agree on what a row
    // looks like or the three screens look like three different games.
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);

    switch (m_page) {
        case Page::General:
            m_menu.setItems({
                {"FULLSCREEN",   onOff(m_fullscreen != 0),    true},
                {"VSYNC",       onOff(m_vsync != 0),         true},
                {"UI SCALE",    percent(m_uiScale),          true},
                {"GRAPHICS >",  "",                         true},
                {"AUDIO >",     "",                         true},
                {"CONTROLS >",  "",                         true},
                {"BACK",        "return",                   true},
            });
            break;

        case Page::Graphics:
            m_menu.setItems({
                {"VSYNC",       onOff(m_vsync != 0),         true},
                {"UI SCALE",    percent(m_uiScale),          true},
                {"BACK",        "return",                   true},
            });
            break;

        case Page::Audio:
            m_menu.setItems({
                {"MASTER",      percent(m_masterVolume),    true},
                {"MUSIC",       percent(m_musicVolume),     true},
                {"SFX",         percent(m_sfxVolume),       true},
                {"BACK",        "return",                   true},
            });
            break;

        case Page::Controls:
            m_menu.setItems({
                {"MOVE",        "A / D or ARROW KEYS",      false},
                {"JUMP",        "SPACE",                    false},
                {"DASH",        "LEFT SHIFT",               false},
                {"INTERACT",    "E",                        false},
                {"ATTACK",      "LEFT MOUSE",               false},
                {"ERA SHIFT",   "Q",                        false},
                {"BACK",        "return",                   true},
            });
            break;
    }

    static_cast<void>(ctx);
}

void SettingsState::setPage(Page page, StateContext& ctx)
{
    m_page = page;
    buildItems(ctx);
}

void SettingsState::adjust(StateContext& ctx, int direction)
{
    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }

    const std::string& label = item->label;
    const auto step = [&direction](int& value, int min, int max, int amount) {
        value = std::clamp(value + direction * amount, min, max);
    };

    Core::ConfigStore& store = ctx.config->store();
    Graphics::Window& window = *ctx.window;

    if (label == "FULLSCREEN") {
        step(m_fullscreen, 0, 1, 1);
        store.setInt("graphics", "fullscreen", m_fullscreen);
        window.setFullscreenDesktop(m_fullscreen != 0);
    } else if (label == "VSYNC") {
        step(m_vsync, 0, 1, 1);
        store.setInt("graphics", "vsync", m_vsync);
        window.setVsync(m_vsync != 0);
    } else if (label == "UI SCALE") {
        step(m_uiScale, 50, 200, 10);
        store.setInt("graphics", "uiScale", m_uiScale);
    } else if (label == "MASTER") {
        step(m_masterVolume, 0, 100, 10);
        store.setInt("audio", "masterVolume", m_masterVolume);
    } else if (label == "MUSIC") {
        step(m_musicVolume, 0, 100, 10);
        store.setInt("audio", "musicVolume", m_musicVolume);
    } else if (label == "SFX") {
        step(m_sfxVolume, 0, 100, 10);
        store.setInt("audio", "sfxVolume", m_sfxVolume);
    }

    buildItems(ctx);
}

void SettingsState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    if (ctx.input == nullptr) {
        return;
    }
    syncScale(ctx);

    if (ctx.input->wasPressed(Action::Pause)) {
        ctx.states->pop();
        return;
    }

    if (ctx.input->wasPressed(Action::MoveUp)) {
        m_menu.move(-1);
    }
    if (ctx.input->wasPressed(Action::MoveDown)) {
        m_menu.move(1);
    }

    // Left/right adjust the highlighted value. Repeat is handled by the input
    // system reporting presses, so holding a key is a series of discrete steps.
    if (ctx.input->wasPressed(Action::MoveLeft)) {
        adjust(ctx, -1);
    }
    if (ctx.input->wasPressed(Action::MoveRight)) {
        adjust(ctx, +1);
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }

    const bool confirm = ctx.input->wasPressed(Action::Interact) || ctx.input->wasPressed(Action::Jump);
    if (!confirm) {
        return;
    }

    if (item->label == "BACK") {
        ctx.states->pop();
    } else if (item->label == "GRAPHICS >") {
        setPage(Page::Graphics, ctx);
    } else if (item->label == "AUDIO >") {
        setPage(Page::Audio, ctx);
    } else if (item->label == "CONTROLS >") {
        setPage(Page::Controls, ctx);
    } else {
        // Numeric or boolean rows toggle when confirmed.
        adjust(ctx, +1);
    }
}

void SettingsState::render(StateContext& ctx, double alpha)
{
    Renderer2D& renderer = *ctx.renderer;
    Graphics::TextRenderer& text = *ctx.text;
    const Rect area = fullArea(renderer);
    static_cast<void>(alpha);

    const char* title = "SETTINGS";
    switch (m_page) {
        case Page::General:  title = "SETTINGS"; break;
        case Page::Graphics: title = "GRAPHICS"; break;
        case Page::Audio:    title = "AUDIO";    break;
        case Page::Controls: title = "CONTROLS"; break;
    }

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Color{0, 0, 0, 0xB0});

    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), title,
                                           "Left / right to change      Esc to go back", text);

    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRect(layout.panel, Palette::PanelBorder);

    text.drawInRect(renderer, layout.heading, title, m_styles.heading);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);
    text.drawInRect(renderer, layout.footer, "Left / right to change      Esc to go back",
                    m_styles.hint);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);
}

} // namespace EraShift::Game
