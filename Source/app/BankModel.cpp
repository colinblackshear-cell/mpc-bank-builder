#include "BankModel.h"

size_t Bank::memoryBytes() const
{
    size_t total = 0;
    for (const auto& p : pads)
        total += p.memoryBytes();
    return total;
}

int Bank::usedPads() const
{
    int n = 0;
    for (const auto& p : pads)
        n += p.isEmpty() ? 0 : 1;
    return n;
}

juce::String padLabel (int pad)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / kPadsPerBank))
         + juce::String (pad % kPadsPerBank + 1).paddedLeft ('0', 2);
}

int noteForPad (int pad)
{
    static const auto map = mpc::Program::defaultPadToNote();
    return map[(size_t) juce::jlimit (0, mpc::kPadCount - 1, pad)];
}

int padForNote (int note)
{
    for (int pad = 0; pad < mpc::kPadCount; ++pad)
        if (noteForPad (pad) == note)
            return pad;
    return -1;
}

Project::Project()
{
    addBank();
}

void Project::setCurrent (int index)
{
    current = juce::jlimit (0, (int) banks.size() - 1, index);
    changed();
}

int Project::addBank (const juce::String& name)
{
    Bank bank;
    bank.name = name.isNotEmpty() ? name : "Kit " + juce::String ((int) banks.size() + 1);
    banks.push_back (std::move (bank));
    current = (int) banks.size() - 1;
    changed();
    return current;
}

void Project::removeBank (int index)
{
    if (index < 0 || index >= (int) banks.size())
        return;

    banks.erase (banks.begin() + index);

    if (banks.empty())
        addBank();

    current = juce::jlimit (0, (int) banks.size() - 1, current);
    changed();
}
