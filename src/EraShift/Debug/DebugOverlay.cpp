#include "EraShift/Debug/DebugOverlay.hpp"

#include "EraShift/Core/Log.hpp"
#include "EraShift/Graphics/BitmapFont.hpp"
#include "EraShift/Graphics/Color.hpp"
#include "EraShift/Graphics/Renderer2D.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>

namespace EraShift::Debug {

using Core::LoopStats;
using Core::StateMachine;
using Graphics::BitmapFont;
using Graphics::Color;
namespace Palette = Graphics::Palette;
using Graphics::Rect;
using Graphics::Renderer2D;
using Graphics::Vec2;

DebugInfoProvider::~DebugInfoProvider() = default;

namespace {

std::string formatFixed(double value, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string formatBytes(std::uint64_t bytes)
{
    static const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
    return buffer;
}

} // namespace

void DebugOverlay::addProvider(std::shared_ptr<DebugInfoProvider> provider)
{
    if (provider != nullptr) {
        m_providers.push_back(std::move(provider));
    }
}

void DebugOverlay::clearProviders()
{
    m_providers.clear();
}

std::vector<DebugOverlay::Line> DebugOverlay::buildLines(const PerformanceStats& stats,
                                                          const StateMachine& states,
                                                          const LoopStats& loop) const
{
    std::vector<Line> lines;
    lines.reserve(24);

    lines.push_back({"FPS", formatFixed(stats.averageFps(), 1), false});
    lines.push_back({"Frame", formatFixed(stats.averageFrameTimeMs(), 2) + " ms", false});
    lines.push_back({"Peak", formatFixed(stats.peakFrameTimeMs(), 2) + " ms", false});
    lines.push_back({"CPU", formatFixed(stats.averageCpuMs(), 2) + " ms", false});
    lines.push_back({"  update", formatFixed(stats.averageUpdateMs(), 2) + " ms", false});
    lines.push_back({"  render", formatFixed(stats.averageRenderMs(), 2) + " ms", false});
    lines.push_back({"Entities", std::to_string(stats.entityCount()), false});
    lines.push_back({"Draw calls", std::to_string(stats.drawCalls()), false});
    lines.push_back({"Memory", formatBytes(stats.residentBytes()), false});
    lines.push_back({"", "", true});

    lines.push_back({"State", std::string(Core::stateName(states.currentId())), false});
    lines.push_back({"Stack", std::to_string(states.depth()), false});
    lines.push_back({"Sim steps", std::to_string(loop.stepsLastFrame), false});
    lines.push_back({"Alpha", formatFixed(loop.simulationAlpha, 3), false});
    lines.push_back({"Backlog", formatFixed(loop.backlogSeconds * 1000.0, 2) + " ms", false});
    if (loop.droppedSteps > 0) {
        lines.push_back({"Dropped", std::to_string(loop.droppedSteps), false});
    }
    lines.push_back({"Frames", std::to_string(stats.totalFrames()), false});

    if (!m_providers.empty()) {
        lines.push_back({"", "", true});
        std::vector<std::pair<std::string, std::string>> extra;
        for (const auto& provider : m_providers) {
            extra.clear();
            provider->collectDebugLines(extra);
            for (const auto& [label, value] : extra) {
                lines.push_back({label, value, false});
            }
        }
    }

    return lines;
}

std::vector<DebugOverlay::Line> DebugOverlay::buildLogLines(Core::LogSink* sink, std::size_t maxLines)
{
    std::vector<Line> lines;
    if (sink == nullptr) {
        return lines;
    }

    const auto* memorySink = dynamic_cast<const Core::MemoryLogSink*>(sink);
    if (memorySink == nullptr) {
        return lines;
    }

    const auto records = memorySink->records();
    const std::size_t start = records.size() > maxLines ? records.size() - maxLines : 0;

    lines.reserve(records.size() - start + 1);
    lines.push_back({"", "", true});
    for (std::size_t i = start; i < records.size(); ++i) {
        std::string text = records[i].message;
        if (text.size() > 52) {
            text.resize(52);
        }
        lines.push_back({std::string(Core::logLevelName(records[i].level)), text, false, true});
    }
    return lines;
}

void DebugOverlay::render(Renderer2D& renderer, Graphics::TextRenderer& textRenderer,
                          const PerformanceStats& stats, const StateMachine& states,
                          const LoopStats& loop)
{
    if (!m_visible || !textRenderer.ready()) {
        return;
    }

    std::vector<Line> lines = buildLines(stats, states, loop);
    const std::vector<Line> logLines = buildLogLines(m_logSink, 6);
    lines.insert(lines.end(), logLines.begin(), logLines.end());

    Graphics::TextStyle labelStyle;
    labelStyle.pixelSize = 15;
    labelStyle.color     = Palette::TextDim;

    Graphics::TextStyle valueStyle;
    valueStyle.pixelSize = 15;
    valueStyle.color     = Palette::TextPrimary;

    // The log tail is denser and dimmer than the counters above it.
    Graphics::TextStyle logStyle = labelStyle;
    logStyle.pixelSize = 13;

    const float padding    = 10.0f;
    const float lineHeight = textRenderer.lineHeight(valueStyle);
    const float logHeight  = textRenderer.lineHeight(logStyle);

    // Measure the real strings so the panel always fits its content, whatever
    // labels the providers contribute.
    float labelWidth = 0.0f;
    float valueWidth = 0.0f;
    for (const Line& line : lines) {
        if (line.separator) {
            continue;
        }
        const Graphics::TextStyle& style = line.dense ? logStyle : valueStyle;
        labelWidth = std::max(labelWidth, textRenderer.measure(line.label, style).x);
        valueWidth = std::max(valueWidth, textRenderer.measure(line.value, style).x);
    }

    const float panelWidth  = labelWidth + valueWidth + padding * 3.0f;
    const float panelHeight = lineHeight * static_cast<float>(lines.size()) + padding * 2.0f;
    const Rect panel{8.0f, 8.0f, panelWidth, panelHeight};

    renderer.setBlendMode(Graphics::BlendMode::Alpha);
    renderer.drawRect(panel, Palette::PanelFill);
    renderer.drawRectOutline(panel, Palette::PanelBorder, 1.0f);

    float y = panel.y + padding;
    for (const Line& line : lines) {
        const float step = line.dense ? logHeight : lineHeight;

        if (line.separator) {
            renderer.drawRect(Rect{panel.x + padding, y + step * 0.5f,
                                   panel.w - padding * 2.0f, 1.0f},
                             Palette::PanelBorder.withAlpha(0x60));
            y += step;
            continue;
        }

        textRenderer.draw(renderer, Vec2{panel.x + padding, y}, line.label,
                          line.dense ? logStyle : labelStyle);
        textRenderer.draw(renderer, Vec2{panel.x + padding + labelWidth, y}, line.value,
                          line.dense ? logStyle : valueStyle);
        y += step;
    }

    renderer.setBlendMode(Graphics::BlendMode::None);
}

} // namespace EraShift::Debug
