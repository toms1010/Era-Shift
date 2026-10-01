#include "EraShift/Game/SettingsDraft.hpp"

#include <algorithm>

namespace EraShift::Game {

std::string SettingSpec::display(int value) const
{
    switch (kind) {
        case SettingKind::Toggle:
            // ON/OFF rather than 1/0: the row is read at a glance while the player
            // is deciding where to look, and a digit there means nothing to them.
            return value != 0 ? "ON" : "OFF";
        case SettingKind::Percent:
            return std::to_string(value) + "%";
        case SettingKind::Scale:
            return std::to_string(value) + "%";
    }
    return std::to_string(value);
}

int SettingSpec::stepped(int value, int direction) const
{
    return std::clamp(value + direction * step, minimum, maximum);
}

void SettingsDraft::add(const SettingSpec& spec)
{
    m_specs.push_back(spec);
    SettingValue value;
    // Both sides start at the loaded value, not at `fallback`: a caller that sets
    // `current` has already read what the game is running with, and starting the
    // draft anywhere else would show a pending change the player never made.
    value.current = spec.current;
    value.pending = spec.current;
    m_values.push_back(value);
}

const SettingValue* SettingsDraft::valueAt(std::size_t index) const noexcept
{
    return index < m_values.size() ? &m_values[index] : nullptr;
}

const SettingSpec* SettingsDraft::specAt(std::size_t index) const noexcept
{
    return index < m_specs.size() ? &m_specs[index] : nullptr;
}

int SettingsDraft::clamp(int value, const SettingSpec& spec) const noexcept
{
    return std::clamp(value, spec.minimum, spec.maximum);
}

void SettingsDraft::adjust(std::size_t index, int direction)
{
    if (index >= m_specs.size() || direction == 0) {
        return;
    }
    m_values[index].pending =
        m_specs[index].stepped(m_values[index].pending, direction < 0 ? -1 : 1);
}

void SettingsDraft::stage(std::size_t index, int value)
{
    if (index >= m_specs.size()) {
        return;
    }
    m_values[index].pending = clamp(value, m_specs[index]);
}

bool SettingsDraft::dirty() const noexcept
{
    for (const SettingValue& value : m_values) {
        if (value.changed()) {
            return true;
        }
    }
    return false;
}

std::vector<std::size_t> SettingsDraft::changedIndices() const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        if (m_values[i].changed()) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<std::size_t> SettingsDraft::commit()
{
    std::vector<std::size_t> committed;
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        if (!m_values[i].changed()) {
            continue;
        }
        m_values[i].current = m_values[i].pending;
        committed.push_back(i);
    }
    return committed;
}

void SettingsDraft::discard()
{
    for (SettingValue& value : m_values) {
        value.pending = value.current;
    }
}

} // namespace EraShift::Game