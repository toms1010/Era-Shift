#include "EraShift/Core/GameState.hpp"

#include <algorithm>
#include <ostream>

namespace EraShift::Core {

std::string_view stateName(GameState state) noexcept
{
    switch (state) {
        case GameState::None:      return "None";
        case GameState::Boot:      return "Boot";
        case GameState::MainMenu:  return "MainMenu";
        case GameState::Settings:  return "Settings";
        case GameState::Playing:   return "Playing";
        case GameState::Paused:    return "Paused";
        case GameState::Inventory: return "Inventory";
        case GameState::Map:       return "Map";
        case GameState::QuestLog:  return "QuestLog";
        case GameState::Loading:   return "Loading";
        case GameState::GameOver:  return "GameOver";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, GameState state)
{
    return os << stateName(state);
}

// ---------------------------------------------------------------------------
// IGameState
// ---------------------------------------------------------------------------
IGameState::IGameState(GameState id) noexcept
    : m_id(id)
{
}

IGameState::~IGameState() = default;

// ---------------------------------------------------------------------------
// StateMachine
// ---------------------------------------------------------------------------
void StateMachine::push(std::shared_ptr<IGameState> state)
{
    if (state == nullptr) {
        return;
    }
    if (m_context != nullptr) {
        if (!m_stack.empty()) {
            m_stack.back()->onPause(*m_context);
            m_stack.back()->notePause();
        }
        state->onEnter(*m_context);
        state->noteEnter();
    }
    m_stack.push_back(std::move(state));
}

bool StateMachine::pop()
{
    if (m_stack.size() <= 1) {
        return false; // the root state cannot be popped
    }

    std::shared_ptr<IGameState> finished = std::move(m_stack.back());
    m_stack.pop_back();

    if (m_context != nullptr) {
        finished->onExit(*m_context);
        if (!m_stack.empty()) {
            m_stack.back()->onResume(*m_context);
        }
    }
    return true;
}

void StateMachine::switchTo(std::shared_ptr<IGameState> state)
{
    if (state == nullptr) {
        return;
    }

    if (m_context != nullptr) {
        while (!m_stack.empty()) {
            std::shared_ptr<IGameState> finished = std::move(m_stack.back());
            m_stack.pop_back();
            finished->onExit(*m_context);
        }
        state->onEnter(*m_context);
        state->noteEnter();
    }

    m_stack.clear();
    m_stack.push_back(std::move(state));
}

void StateMachine::popToRoot()
{
    if (m_context == nullptr) {
        m_stack.resize(std::min<std::size_t>(m_stack.size(), 1));
        return;
    }
    while (m_stack.size() > 1) {
        std::shared_ptr<IGameState> finished = std::move(m_stack.back());
        m_stack.pop_back();
        finished->onExit(*m_context);
    }
    if (!m_stack.empty()) {
        m_stack.front()->onResume(*m_context);
    }
}

void StateMachine::reset(std::shared_ptr<IGameState> state)
{
    if (m_context != nullptr) {
        for (auto& s : m_stack) {
            s->onExit(*m_context);
        }
    }
    m_stack.clear();
    push(std::move(state));
}

IGameState* StateMachine::current() const noexcept
{
    return m_stack.empty() ? nullptr : m_stack.back().get();
}

GameState StateMachine::currentId() const noexcept
{
    const IGameState* top = current();
    return top != nullptr ? top->id() : GameState::None;
}

bool StateMachine::isInStack(GameState state) const noexcept
{
    return std::any_of(m_stack.begin(), m_stack.end(),
                       [state](const std::shared_ptr<IGameState>& s) { return s->id() == state; });
}

void StateMachine::update(double fixedDelta)
{
    IGameState* top = current();
    if (top == nullptr || m_context == nullptr) {
        return;
    }
    // Only the top of the stack simulates. States below it were paused when it
    // was pushed, so they correctly do not tick while a menu is open.
    top->update(*m_context, fixedDelta);
}

void StateMachine::render(double alpha)
{
    if (m_context == nullptr) {
        return;
    }
    // Render bottom-up so overlays (pause, inventory) draw above gameplay.
    for (auto& state : m_stack) {
        state->render(*m_context, alpha);
    }
}

std::vector<std::string> StateMachine::stackTrace() const
{
    std::vector<std::string> names;
    names.reserve(m_stack.size());
    for (const auto& state : m_stack) {
        names.emplace_back(stateName(state->id()));
    }
    return names;
}

} // namespace EraShift::Core
