#include "EraShift/Game/states/PausedState.hpp"

#include "EraShift/Game/states/MainMenuState.hpp"
#include "EraShift/Game/states/PlayingState.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"
#include "EraShift/Input/InputManager.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>

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

std::string uppercase(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string describeBinding(const Input::InputBinding& binding)
{
    switch (binding.type) {
        case Input::InputBinding::Type::None:          return "unbound";
        case Input::InputBinding::Type::Key:           return std::string(Input::keyName(binding.key));
        case Input::InputBinding::Type::MouseButton:   return std::string(Input::mouseButtonName(binding.button));
    }
    return "unbound";
}

/// Strips the trailing marker the controls page adds to the row being rebound.
std::string trimMarker(const std::string& label)
{
    std::string out = label;
    while (!out.empty() && (out.back() == '*' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
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
        {"RESUME",    "return to the timeline", "", true},
        {"SETTINGS",  "options",                 "", true},
        {"MAIN MENU", "abandon this run",        "", true},
        {"QUIT",      "exit to desktop",         "", true},
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
    renderer.drawRectOutline(layout.panel, Palette::PanelBorder, 2.0f);

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
    m_uiScale       = store.getInt("graphics", "uiScale", 100);

    buildItems(ctx);
    ctx.log->info("Game", "entered Settings");
}

void SettingsState::onExit(StateContext& ctx)
{
    if (m_rebind == Rebind::Waiting) {
        // Leaving mid-capture would otherwise leave the screen waiting for a key
        // press that can no longer arrive.
        m_rebind = Rebind::None;
    }
    ctx.config->saveUserConfig(*ctx.log);
    ctx.log->info("Game", "settings saved");
}

void SettingsState::buildItems(StateContext& ctx)
{
    // Row metrics come from the shared style, never from a literal: the pause
    // menu, the settings screen and the main menu must agree on what a row
    // looks like or the three screens look like three different games.
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);

    // One shared builder so a value can never appear in the list but not on the
    // screen: the label is the key, the value is the display text.
    const auto option = [](std::string label, std::string value, bool enabled = true) {
        MenuItem item;
        item.label   = std::move(label);
        item.value   = std::move(value);
        item.enabled = enabled;
        return item;
    };

    switch (m_page) {
        case Page::General:
            m_menu.setItems({
                option("FULLSCREEN", onOff(m_fullscreen != 0)),
                option("VSYNC", onOff(m_vsync != 0)),
                option("UI SCALE", percent(m_uiScale)),
                {"GRAPHICS >", "resolution and presentation", true},
                {"CONTROLS >", "rebind the keyboard and mouse", true},
                {"BACK", "return", true},
            });
            break;

        case Page::Graphics:
            m_menu.setItems({
                option("VSYNC", onOff(m_vsync != 0)),
                option("UI SCALE", percent(m_uiScale)),
                {"BACK", "return", true},
            });
            break;

        case Page::Controls:
            m_menu.setItems(controlItems(ctx));
            break;
    }

    static_cast<void>(ctx);
}

std::vector<MenuItem> SettingsState::controlItems(StateContext& ctx) const
{
    std::vector<MenuItem> items;
    const Input::InputManager& input = *ctx.input;
    static_cast<void>(ctx);

    for (const Input::Action action : controlActions()) {
        const Input::ActionBinding binding = input.bindingOf(action);
        std::string value;
        if (!binding.primary.empty()) {
            value = describeBinding(binding.primary);
        }
        if (!binding.secondary.empty()) {
            value += "  /  " + describeBinding(binding.secondary);
        }

        std::string label = uppercase(actionName(action));
        if (m_rebind == Rebind::Waiting && action == m_rebindTarget) {
            // The prompt replaces the value so the change is impossible to miss.
            value = "press a key or mouse button...";
            label += "  *";
        }

        MenuItem item;
        item.label   = label;
        item.value   = value;
        item.detail  = std::string("rebind ") + std::string(actionName(action));
        item.enabled = true;
        items.push_back(item);
    }

    MenuItem back;
    back.label  = "BACK";
    back.detail = "return";
    items.push_back(back);
    return items;
}

const std::vector<Input::Action>& SettingsState::controlActions()
{
    // The actions a player actually rebinds, which is the list of actions that
    // something actually reads.
    //
    // `Aim` used to be here. It is bound to a key and it appeared in this list as
    // a rebindable row, and it did nothing at all - there is no aiming in a
    // melee-only game, so no consumer ever read the action. A row in a settings
    // screen is a promise that the control does something, and offering one that
    // does not is worse than not offering it: the player rebinds it, sees no
    // change, and concludes the rebinding is broken rather than that the row was
    // a lie.
    //
    // Debug and screenshot are left alone deliberately: a player who rebound those
    // away would have no way to get the overlay or a capture back.
    static const std::vector<Input::Action> kActions = {
        Input::Action::MoveLeft, Input::Action::MoveRight, Input::Action::MoveUp,
        Input::Action::MoveDown, Input::Action::Jump,     Input::Action::Dash,
        Input::Action::Interact, Input::Action::Attack,   Input::Action::HeavyAttack,
        Input::Action::ShiftEra,
    };
    return kActions;
}

void SettingsState::setPage(Page page, StateContext& ctx)
{
    m_page   = page;
    m_rebind = Rebind::None;
    buildItems(ctx);
}

bool SettingsState::isAdjustable(const std::string& label) const
{
    return label == "FULLSCREEN" || label == "VSYNC" || label == "UI SCALE";
}

void SettingsState::persistBinding(StateContext& ctx, Input::Action action,
                                  const Input::InputBinding& binding)
{
    if (ctx.config == nullptr) {
        return;
    }

    // Written back through the same config table `loadBindings` reads, so a
    // rebound key survives a restart without a second format to keep in step.
    Core::ConfigStore& store = ctx.config->store();
    const std::string name(actionName(action));
    switch (binding.type) {
        case Input::InputBinding::Type::None:
            store.setString("controls", name, "");
            break;
        case Input::InputBinding::Type::Key:
            store.setString("controls", name, std::string(Input::keyName(binding.key)));
            break;
        case Input::InputBinding::Type::MouseButton:
            store.setString("controls", name,
                            std::string("Mouse ") + std::string(Input::mouseButtonName(binding.button)));
            break;
    }
}

void SettingsState::updateRebinding(StateContext& ctx)
{
    if (m_rebind == Rebind::Waiting) {
        // Escape backs out; anything else becomes the new binding. Waiting for
        // the *next* press rather than the current one is what stops the Enter
        // that started the rebind from immediately binding itself.
        if (ctx.input->wasKeyPressed(Input::Key::Escape)) {
            m_rebind = Rebind::Cancelled;
            buildItems(ctx);
            ctx.log->info("Settings", "rebind cancelled");
            return;
        }

        std::optional<Input::InputBinding> captured;
        for (std::size_t i = 0; i < static_cast<std::size_t>(Input::Key::Count); ++i) {
            const auto key = static_cast<Input::Key>(i);
            if (!ctx.input->wasKeyPressed(key)) {
                continue;
            }
            Input::InputBinding binding;
            binding.type = Input::InputBinding::Type::Key;
            binding.key   = key;
            captured      = binding;
            break;
        }
        if (!captured.has_value()) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(Input::MouseButton::Count); ++i) {
                const auto button = static_cast<Input::MouseButton>(i);
                if (!ctx.input->wasMousePressed(button)) {
                    continue;
                }
                Input::InputBinding binding;
                binding.type   = Input::InputBinding::Type::MouseButton;
                binding.button = button;
                captured        = binding;
                break;
            }
        }
        if (!captured.has_value()) {
            return;
        }

        ctx.input->rebind(m_rebindTarget, *captured);
        persistBinding(ctx, m_rebindTarget, *captured);
        ctx.log->info("Settings", "{} rebound to {}", actionName(m_rebindTarget),
                      describeBinding(*captured));
        m_rebind = Rebind::None;
        buildItems(ctx);
        return;
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr || item->label.empty() || item->label == "BACK") {
        return;
    }
    // The label carries a marker while a rebind is in flight; a plain label is
    // the action name uppercased.
    const std::string name = trimMarker(item->label);
    for (const Input::Action action : controlActions()) {
        if (uppercase(actionName(action)) == name) {
            m_rebindTarget = action;
            m_rebind       = Rebind::Waiting;
            buildItems(ctx);
            return;
        }
    }
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
        // The scale is picked up by `uiScaleFor` from the store, and the next
        // syncScale() applies it, so there is nothing further to do here.
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

    // While a rebind is being captured, only the rebind logic runs. Letting the
    // menu keep navigating underneath means the row being listened to slides
    // away from the cursor the moment the player presses a key.
    if (m_rebind == Rebind::Waiting) {
        updateRebinding(ctx);
        return;
    }

    if (ctx.input->wasPressed(Action::Pause)) {
        if (m_page != Page::General) {
            m_page = Page::General;
            m_rebind = Rebind::None;
            buildItems(ctx);
            return;
        }
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
        if (m_page == Page::General) {
            ctx.states->pop();
        } else {
            m_page = Page::General;
            buildItems(ctx);
        }
    } else if (item->label == "GRAPHICS >") {
        setPage(Page::Graphics, ctx);
    } else if (item->label == "CONTROLS >") {
        setPage(Page::Controls, ctx);
    } else if (m_page == Page::Controls) {
        updateRebinding(ctx);
    } else if (isAdjustable(item->label)) {
        // Numeric and boolean rows toggle when confirmed.
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
        case Page::Controls: title = "CONTROLS"; break;
    }

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Color{0, 0, 0, 0xB0});

    const char* footer = m_page == Page::Controls
                             ? "Enter to rebind      Esc to go back"
                             : "Left / right to change      Esc to go back";
    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), title, footer,
                                           text);

    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRectOutline(layout.panel, Palette::PanelBorder, 2.0f);

    text.drawInRect(renderer, layout.heading, title, m_styles.heading);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles);
    text.drawInRect(renderer, layout.footer, footer, m_styles.hint);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);
}

} // namespace EraShift::Game
