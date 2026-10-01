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

    // Whether the tutorial is running decides whether SKIP TUTORIAL is offered, so
    // it is read before the items are built. The progression database is the only
    // authority on this: the flag lives there, not in the config store.
    m_tutorialRunning = false;
    if (ctx.progress != nullptr) {
        const Core::Progress progress = ctx.progress->loadProgress();
        m_tutorialRunning = progress.tutorialEnabled && !progress.tutorialComplete;
    }
    m_viewportScale = uiScaleFor(ctx);
    m_styles = MenuStyles::make(m_viewportScale);
    buildPauseMenu();
    ctx.log->info("Game", "paused");
}

void PausedState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    // Both are needed to lay the panel out for hit testing, so neither being
    // present means there is nothing to hit test against.
    if (ctx.input == nullptr || ctx.renderer == nullptr || ctx.text == nullptr) {
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

    // The mouse has to select and activate here, exactly as it does on the main
    // menu and the results screen. The rows are drawn with a hover highlight, so
    // a pause menu that ignores the pointer shows the player a button that
    // lights up when pointed at and then does nothing when clicked.
    const Rect area = fullArea(*ctx.renderer);
    const PanelLayout layout =
        layoutPanel(area, m_styles, m_menu.items().size(), "PAUSED", "ESC to resume", *ctx.text);
    const auto hovered = m_menu.hitTest(layout.list, ctx.input->mousePosition(), m_styles);
    if (hovered != static_cast<std::size_t>(-1)) {
        m_menu.select(hovered);
    }

    const MenuItem* item = m_menu.currentItem();
    if (item == nullptr) {
        return;
    }

    const bool confirm = ctx.input->wasPressed(Action::Interact) ||
                         ctx.input->wasPressed(Action::Jump) ||
                         (hovered != static_cast<std::size_t>(-1) &&
                          ctx.input->wasMousePressed(Input::MouseButton::Left));
    if (!confirm) {
        return;
    }

    if (item->label == "RESUME") {
        ctx.states->pop();
    } else if (item->label == "SKIP TUTORIAL") {
        // Two presses, the same shape as RESET PROGRESS, because both destroy
        // something the player may want back.
        //
        // The first press returns to the run with the row armed. The run is not
        // paused any more, so a player who wants to undo it re-opens the pause
        // menu and sees the row still asking — and can change their mind.
        if (m_confirmSkip) {
            m_confirmSkip = false;
            // Marked *complete* rather than merely switched off, so the lessons do
            // not return on the next region. Setting the toggle off would hide them
            // until the player switched it back on, which is not skipping.
            if (ctx.progress != nullptr) {
                Core::Progress progress = ctx.progress->loadProgress();
                progress.tutorialComplete = true;
                ctx.progress->saveProgress(progress);
                if (ctx.log != nullptr) {
                    ctx.log->info("Game", "tutorial skipped");
                }
            }
        } else {
            m_confirmSkip = true;
            if (ctx.log != nullptr) {
                ctx.log->info("Game", "skip requested: press again to confirm");
            }
        }
    } else if (item->label == "SETTINGS") {
        ctx.states->push(std::make_shared<SettingsState>());
    } else if (item->label == "MAIN MENU") {
        ctx.states->popToRoot();
    } else if (item->label == "QUIT") {
        ctx.loop->requestStop();
    }
}

void PausedState::buildPauseMenu()
{
    m_menu.setMetrics(m_styles.rowHeight, m_styles.rowSpacing);
    m_menu.setItems({
        {"RESUME",    "return to the timeline", "", true},
        {"SETTINGS",  "options",                 "", true},
        // Offered only while the tutorial is actually running. A skip button that
        // is always there reads as "this thing is optional and you probably should
        // dismiss it", which is the opposite of what it is for.
        {"SKIP TUTORIAL",
         m_confirmSkip ? "press again to stop the lessons" : "stop showing the lessons",
         "", m_tutorialRunning},
        {"MAIN MENU", "abandon this run",        "", true},
        {"QUIT",      "exit to desktop",         "", true},
    });
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
    m_menu.ensureVisible(layout.visibleRows, m_styles);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  layout.visibleRows, static_cast<float>(m_time));
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

    // Read current values out of the config store so the screen reflects what the
    // game is actually using, then stage them in a draft. Nothing is written from
    // here: BACK discards the draft, and only an explicit APPLY commits it.
    buildDraft(ctx);
    buildItems(ctx);
    ctx.log->info("Game", "entered Settings");
}

void SettingsState::buildDraft(StateContext& ctx)
{
    m_draft = Game::SettingsDraft{};

    const auto add = [this, &ctx](const char* label, const char* key, Game::SettingKind kind,
                                  int lo, int hi, int step, int fallback,
                                  const char* detail) {
        Game::SettingSpec spec;
        spec.label    = label;
        spec.section  = "graphics";
        spec.key      = key;
        spec.kind     = kind;
        spec.minimum  = lo;
        spec.maximum  = hi;
        spec.step     = step;
        spec.fallback = fallback;
        spec.detail   = detail;

        if (ctx.config != nullptr) {
            spec.current = ctx.config->store().getInt(spec.section, spec.key, fallback);
        }
        m_draft.add(spec);
    };

    add("FULLSCREEN", "fullscreen", Game::SettingKind::Toggle, 0, 1, 1, 0,
        "leave the window");
    add("VSYNC", "vsync", Game::SettingKind::Toggle, 0, 1, 1, 1,
        "wait for the display instead of tearing");
    add("UI SCALE", "uiScale", Game::SettingKind::Scale, 50, 200, 10, 100,
        "size of every piece of interface text");

    // The tutorial flag is progression, not configuration: it lives in the database
    // because New Game resets that, and a setting that a New Game silently discards
    // is not a setting.
    Game::SettingSpec tutorial;
    tutorial.label    = "TUTORIAL";
    tutorial.section  = "tutorial";
    tutorial.key      = "enabled";
    tutorial.kind     = Game::SettingKind::Toggle;
    tutorial.minimum  = 0;
    tutorial.maximum  = 1;
    tutorial.step     = 1;
    tutorial.fallback = 1;
    tutorial.detail   = "contextual lessons on the first region";
    if (ctx.progress != nullptr) {
        tutorial.current = ctx.progress->loadProgress().tutorialEnabled ? 1 : 0;
    }
    m_draft.add(tutorial);

    m_fullscreen    = settingOf("fullscreen");
    m_vsync         = settingOf("vsync");
    m_uiScale       = settingOf("uiScale");
    m_tutorialEnabled = settingOf("tutorial");
    m_showFps       = ctx.config != nullptr
                          ? ctx.config->store().getInt("debug", "showStats", 0)
                          : 0;
}

std::size_t SettingsState::draftIndexOf(const char* key) const
{
    for (std::size_t i = 0; i < m_draft.size(); ++i) {
        const Game::SettingSpec* spec = m_draft.specAt(i);
        if (spec != nullptr && spec->key == key) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

int SettingsState::settingOf(const char* key) const
{
    for (std::size_t i = 0; i < m_draft.size(); ++i) {
        const Game::SettingSpec* spec = m_draft.specAt(i);
        if (spec != nullptr && spec->key == key) {
            const Game::SettingValue* value = m_draft.valueAt(i);
            return value != nullptr ? value->current : spec->fallback;
        }
    }
    return 0;
}

int SettingsState::displayedOf(const char* key) const
{
    for (std::size_t i = 0; i < m_draft.size(); ++i) {
        const Game::SettingSpec* spec = m_draft.specAt(i);
        if (spec != nullptr && spec->key == key) {
            const Game::SettingValue* value = m_draft.valueAt(i);
            if (value == nullptr) {
                return spec->fallback;
            }
            return value->pending;
        }
    }
    return 0;
}

std::size_t SettingsState::indexOfLabel(const std::string& label) const
{
    for (std::size_t i = 0; i < m_menu.items().size(); ++i) {
        if (m_menu.items()[i].label == label) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

void SettingsState::applyDraft(StateContext& ctx)
{
    const std::vector<std::size_t> committed = m_draft.commit();
    if (committed.empty()) {
        if (ctx.log != nullptr) {
            ctx.log->info("Settings", "nothing to apply");
        }
        return;
    }

    for (const std::size_t index : committed) {
        const Game::SettingSpec* spec   = m_draft.specAt(index);
        const Game::SettingValue* value = m_draft.valueAt(index);
        if (spec == nullptr || value == nullptr) {
            continue;
        }

        // Persist first, apply second. If the write fails the game is still running
        // the old value, which is a coherent state; the other order would leave the
        // window disagreeing with the file.
        const bool isTutorial = spec->section == "tutorial";
        bool written = false;

        if (isTutorial) {
            if (ctx.progress != nullptr) {
                Core::Progress progress = ctx.progress->loadProgress();
                progress.tutorialEnabled = value->current != 0;
                written = ctx.progress->saveProgress(progress);
            }
        } else if (ctx.config != nullptr) {
            ctx.config->store().setInt(spec->section, spec->key, value->current);
            written = ctx.config->saveUserConfig(*ctx.log);
        }

        // Applied to the live subsystem only after the value is safely stored.
        if (written) {
            if (spec->key == "fullscreen" && ctx.window != nullptr) {
                ctx.window->setFullscreenDesktop(value->current != 0);
            } else if (spec->key == "vsync" && ctx.window != nullptr) {
                ctx.window->setVsync(value->current != 0);
            }
        }

        if (ctx.log != nullptr) {
            ctx.log->info("Settings", "{} = {}", spec->key, spec->display(value->current));
        }
    }

    // The live values, so `buildItems` shows what the game is running rather than
    // what the draft used to say.
    m_fullscreen     = settingOf("fullscreen");
    m_vsync          = settingOf("vsync");
    m_uiScale        = settingOf("uiScale");
    m_tutorialEnabled = settingOf("tutorial");
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
                option("FULLSCREEN", onOff(displayedOf("fullscreen") != 0)),
                option("VSYNC", onOff(displayedOf("vsync") != 0)),
                option("UI SCALE", percent(displayedOf("uiScale"))),
                {"GRAPHICS >", "resolution and presentation", true},
                {"GAME >", "tutorial and progression", true},
                {"CONTROLS >", "rebind the keyboard and mouse", true},
                {"APPLY", m_draft.dirty() ? "save the changes above" : "nothing to save",
                 m_draft.dirty()},
                {"BACK", m_draft.dirty() ? "discard the changes and return" : "return",
                 true},
            });
            break;

        case Page::Game:
            // The tutorial toggle is on its own page because it is a *recorded*
            // preference, not a graphics one: it lives in the progression
            // database rather than in settings.json, and putting it on the
            // graphics page would imply otherwise.
            m_menu.setItems({
                option("TUTORIAL", onOff(displayedOf("tutorial"))),
                // The label changes rather than a dialog appearing over it, so the
                // confirmation is the thing the player is looking at when they
                // press the key a second time.
                {"RESET PROGRESS",
                 m_confirmReset ? "press again to erase everything" : "lock every region again",
                 true},
                {"APPLY", m_draft.dirty() ? "save the changes above" : "nothing to save",
                 m_draft.dirty()},
                {"BACK", m_draft.dirty() ? "discard the changes and return" : "return",
                 true},
            });
            break;

        case Page::Graphics:
            m_menu.setItems({
                option("VSYNC", onOff(displayedOf("vsync") != 0)),
                option("UI SCALE", percent(displayedOf("uiScale"))),
                {"APPLY", m_draft.dirty() ? "save the changes above" : "nothing to save",
                 m_draft.dirty()},
                {"BACK", m_draft.dirty() ? "discard the changes and return" : "return", true},
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

const char* SettingsState::pageTitle() const noexcept
{
    switch (m_page) {
        case Page::General:  return "SETTINGS";
        case Page::Graphics: return "GRAPHICS";
        case Page::Game:     return "GAME";
        case Page::Controls: return "CONTROLS";
    }
    return "SETTINGS";
}

const char* SettingsState::pageFooter() const noexcept
{
    return m_page == Page::Controls ? "Enter to rebind      Esc to go back"
                                    : "Left / right to change      Esc to go back";
}

void SettingsState::setPage(Page page, StateContext& ctx)
{
    // Leaving a page cancels anything armed on it. A half-armed confirmation that
    // survives navigation is the kind of thing that wipes progress later.
    m_page         = page;
    m_rebind       = Rebind::None;
    m_confirmReset = false;
    buildItems(ctx);
}

bool SettingsState::isAdjustable(const std::string& label) const
{
    return label == "FULLSCREEN" || label == "VSYNC" || label == "UI SCALE" ||
           label == "TUTORIAL";
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
            // Just the button's own name. Prefixing "Mouse " here and comparing the
            // whole string in `parseMouseButton` worked only because the names
            // happen not to start with it - and then the settings screen wrote
            // "Mouse Mouse Left" the moment a binding was rebound, which the loader
            // could not read back. The loader accepts both spellings, so the writer
            // writes the one the loader produces.
            store.setString("controls", name,
                            std::string(Input::mouseButtonName(binding.button)));
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
    if (item == nullptr || ctx.config == nullptr) {
        return;
    }
    static_cast<void>(ctx);

    const std::string& label = item->label;

    // Page links and the two action rows have no value to change, so left and right
    // do nothing on them. Stepping "APPLY" would be meaningless, and stepping
    // "RESET PROGRESS" would be a destructive one-key action.
    if (label == "GRAPHICS >" || label == "GAME >" || label == "CONTROLS >" ||
        label == "BACK" || label == "APPLY" || label == "RESET PROGRESS") {
        return;
    }

    // Maps the row's display label back to its draft row. Going through the draft
    // rather than writing a key here is what makes BACK able to discard.
    const char* key = nullptr;
    if (label == "FULLSCREEN") {
        key = "fullscreen";
    } else if (label == "VSYNC") {
        key = "vsync";
    } else if (label == "UI SCALE") {
        key = "uiScale";
    } else if (label == "TUTORIAL") {
        key = "tutorial";
    } else {
        return;
    }

    for (std::size_t i = 0; i < m_draft.size(); ++i) {
        const Game::SettingSpec* spec = m_draft.specAt(i);
        if (spec == nullptr || spec->key != key) {
            continue;
        }
        m_draft.adjust(i, direction);
        if (ctx.log != nullptr) {
            const Game::SettingValue* value = m_draft.valueAt(i);
            ctx.log->debug("Settings", "{} staged as {} (not yet applied)", key,
                           spec->display(value != nullptr ? value->pending : 0));
        }
        break;
    }

    buildItems(ctx);
}

void SettingsState::discardDraft()
{
    // BACK, ESC and leaving the screen all land here. Nothing was written, so
    // there is nothing to undo — only the staged values to drop.
    m_draft.discard();
    m_fullscreen     = settingOf("fullscreen");
    m_vsync          = settingOf("vsync");
    m_uiScale        = settingOf("uiScale");
    m_tutorialEnabled = settingOf("tutorial");
}

void SettingsState::resetProgress(StateContext& ctx)
{
    if (ctx.progress == nullptr) {
        if (ctx.log != nullptr) {
            ctx.log->warn("Settings", "nothing to reset: no progression database");
        }
        return;
    }
    ctx.progress->resetAll();

    // Resetting invalidates the staged tutorial flag too, or the screen would offer
    // to "save" a value the player had already been told was wiped.
    m_draft.stage(draftIndexOf("tutorial"), 1);
    m_tutorialEnabled = 1;

    // The reset already wiped the flag; re-reading and re-saving is what makes the
    // row's value true in the database rather than only on screen.
    ctx.progress->saveProgress(ctx.progress->loadProgress());

    if (ctx.log != nullptr) {
        ctx.log->info("Settings", "progress reset: every region locked again");
    }
    buildItems(ctx);
}

void SettingsState::update(StateContext& ctx, double fixedDelta)
{
    m_time += static_cast<float>(fixedDelta);

    // As in `PausedState`: hit testing needs the renderer to measure the panel.
    if (ctx.input == nullptr || ctx.renderer == nullptr || ctx.text == nullptr) {
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

    // Escape and BACK both mean "discard", so neither can commit by accident. From
    // a sub-page it goes up a level and *also* discards, because a player who
    // pressed ESC wants out of the whole screen, not just the page.
    if (ctx.input->wasPressed(Action::Pause)) {
        discardDraft();
        if (m_page != Page::General) {
            m_page = Page::General;
            m_rebind = Rebind::None;
            m_confirmReset = false;
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

    // The pointer selects and activates, as on every other menu. A settings row
    // that highlights under the cursor and ignores the click is a dead control,
    // and the settings screen is where a player most expects the mouse to work.
    const Rect area = fullArea(*ctx.renderer);
    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), pageTitle(),
                                           pageFooter(), *ctx.text);
    const auto hovered = m_menu.hitTest(layout.list, ctx.input->mousePosition(), m_styles);
    if (hovered != static_cast<std::size_t>(-1)) {
        m_menu.select(hovered);
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

    const bool confirm = ctx.input->wasPressed(Action::Interact) ||
                         ctx.input->wasPressed(Action::Jump) ||
                         (hovered != static_cast<std::size_t>(-1) &&
                          ctx.input->wasMousePressed(Input::MouseButton::Left));
    if (!confirm) {
        return;
    }

    if (item->label == "APPLY") {
        // The only path that writes. Everything else on this screen is staging.
        applyDraft(ctx);
        buildItems(ctx);
    } else if (item->label == "BACK") {
        discardDraft();
        if (m_page == Page::General) {
            ctx.states->pop();
        } else {
            m_page = Page::General;
            m_confirmReset = false;
            buildItems(ctx);
        }
    } else if (item->label == "GRAPHICS >") {
        setPage(Page::Graphics, ctx);
    } else if (item->label == "GAME >") {
        setPage(Page::Game, ctx);
    } else if (item->label == "CONTROLS >") {
        setPage(Page::Controls, ctx);
    } else if (item->label == "RESET PROGRESS") {
        // Confirm rather than act. This is the only menu row in the game that
        // destroys something, and a single stray press should not be able to wipe
        // a player's ten regions.
        if (m_confirmReset) {
            resetProgress(ctx);
            m_confirmReset = false;
        } else {
            m_confirmReset = true;
            if (ctx.log != nullptr) {
                ctx.log->info("Settings", "reset requested: confirm to wipe progress");
            }
            buildItems(ctx);
        }
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

    const char* title = pageTitle();

    renderer.setCameraEnabled(false);
    renderer.setBlendMode(BlendMode::Alpha);
    renderer.drawRect(area, Color{0, 0, 0, 0xB0});

    const char* footer = pageFooter();
    const PanelLayout layout = layoutPanel(area, m_styles, m_menu.items().size(), title, footer,
                                           text);

    renderer.drawRect(layout.panel, Palette::PanelFill);
    renderer.drawRectOutline(layout.panel, Palette::PanelBorder, 2.0f);

    text.drawInRect(renderer, layout.heading, title, m_styles.heading);
    m_menu.ensureVisible(layout.visibleRows, m_styles);
    m_menu.render(renderer, text, layout.list,
                  ctx.input != nullptr ? ctx.input->mousePosition() : Vec2{}, m_styles,
                  layout.visibleRows, static_cast<float>(m_time));
    text.drawInRect(renderer, layout.footer, footer, m_styles.hint);

    if (ctx.overlay != nullptr && ctx.stats != nullptr) {
        ctx.overlay->render(renderer, text, *ctx.stats, *ctx.states, ctx.loop->stats());
    }

    renderer.setBlendMode(BlendMode::None);
}

} // namespace EraShift::Game
