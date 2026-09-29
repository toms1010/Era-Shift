// Era Shift - developer debug overlay.
//
// Toggled with F3. Draws the frame timing, workload counters and the current
// engine state, plus a rolling log tail. Text goes through TextRenderer so the
// overlay is as readable as the rest of the UI; the built-in bitmap font remains
// the fallback if no TTF face could be loaded.

#pragma once

#include "EraShift/Core/Log.hpp"
#include "EraShift/Core/GameLoop.hpp"
#include "EraShift/Core/GameState.hpp"
#include "EraShift/Debug/PerformanceStats.hpp"
#include "EraShift/Graphics/TextRenderer.hpp"

#include <string>
#include <vector>

namespace EraShift::Graphics {
class Renderer2D;
class TextRenderer;
}

namespace EraShift::Debug {

/// Extra rows contributed by gameplay systems.
///
/// The overlay must not know about entities, paradox or era state, so those
/// systems push a labelled string each frame instead. This keeps the debug
/// layer free of gameplay dependencies.
class DebugInfoProvider {
public:
    virtual ~DebugInfoProvider();
    virtual void collectDebugLines(std::vector<std::pair<std::string, std::string>>& out) = 0;
};

class DebugOverlay {
public:
    DebugOverlay() = default;

    /// Registers a provider. Ownership is shared because several systems
    /// register the same kind of object and the overlay outlives them.
    void addProvider(std::shared_ptr<DebugInfoProvider> provider);
    void clearProviders();

    void setLogSink(Core::LogSink* sink) noexcept { m_logSink = sink; }

    [[nodiscard]] bool visible() const noexcept { return m_visible; }
    void toggle() noexcept { m_visible = !m_visible; }
    void setVisible(bool visible) noexcept { m_visible = visible; }

    /// Draws the overlay. Must be called with the camera disabled so the text
    /// is anchored to the screen.
    void render(Graphics::Renderer2D& renderer, Graphics::TextRenderer& text,
                const PerformanceStats& stats, const Core::StateMachine& states,
                const Core::LoopStats& loop);

private:
    struct Line {
        std::string label;
        std::string value;
        bool separator = false;
        bool dense     = false;  ///< Drawn in the smaller log style.
    };

    std::vector<Line> buildLines(const PerformanceStats& stats,
                                 const Core::StateMachine& states,
                                 const Core::LoopStats& loop) const;
    static std::vector<Line> buildLogLines(Core::LogSink* sink, std::size_t maxLines);

    bool m_visible = false;
    std::vector<std::shared_ptr<DebugInfoProvider>> m_providers;
    Core::LogSink* m_logSink = nullptr;
};

} // namespace EraShift::Debug
