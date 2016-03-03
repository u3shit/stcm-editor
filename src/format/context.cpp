#include "context.hpp"
#include "item.hpp"
#include "../utils.hpp"
#include "../except.hpp"
#include <boost/assert.hpp>
#include <iomanip>
#include <fstream>
#include <sstream>

void Context::SetRoot(std::unique_ptr<Item> nroot)
{
    BOOST_ASSERT(nroot->ctx == this && nroot->position == 0);
    PointerMap npmap{{0, nroot.get()}};

    pmap = std::move(npmap);
    root = std::move(nroot);
    size = root->GetSize();
}

const Label* Context::GetLabel(const std::string& name) const
{
    auto it = labels.find(name);
    if (it == labels.end())
        THROW(boost::enable_error_info(std::out_of_range{"Context::GetLabel"})
              << AffectedLabel{name});
    return &*it;
}

const Label* Context::CreateLabel(std::string name, ItemPointer ptr)
{
    auto pair = labels.insert({std::move(name), ptr});
    if (!pair.second)
        THROW(boost::enable_error_info(std::out_of_range{"label already exists"})
              << AffectedLabel(name));

    return PostCreateLabel(pair, ptr);
}

const Label* Context::CreateLabelFallback(const std::string& name, ItemPointer ptr)
{
    auto pair = labels.insert({name, ptr});
    for (int i = 1; !pair.second; ++i)
    {
        std::stringstream ss;
        ss << name << '_' << i;
        pair = labels.insert({ss.str(), ptr});
    }
    return PostCreateLabel(pair, ptr);
}

const Label* Context::PostCreateLabel(
    std::pair<LabelsMap::iterator, bool> pair, ItemPointer ptr)
{
    Label* item = &*pair.first;
    try
    {
        ptr.item->labels.insert({ptr.offset, item});
    }
    catch (...)
    {
        labels.erase(pair.first);
        throw;
    }
    return item;
}

const Label* Context::GetLabelTo(ItemPointer ptr)
{
    auto& lctr = ptr.item->labels;
    auto it = lctr.find(ptr.offset);
    if (it != lctr.end()) return it->second;

    std::stringstream ss;
    ss << "loc_" << std::setw(8) << std::setfill('0') << std::hex
       << ptr.item->GetPosition() + ptr.offset;
    return CreateLabelFallback(ss.str(), ptr);
}

ItemPointer Context::GetPointer(FilePosition pos) const noexcept
{
    auto it = pmap.upper_bound(pos);
    BOOST_ASSERT(it != pmap.begin());
    --it;
    return {it->second, pos - it->first};
}

void Context::UpdatePositions()
{
    pmap.clear();
    if (GetRoot())
        size = GetRoot()->UpdatePositions(0);
    else
        size = 0;
}

void Context::Dump_(std::ostream& os) const
{
    for (auto it = GetRoot(); it; it = it->GetNext())
        it->Dump(os);
}

void Context::Inspect_(std::ostream& os) const
{
    if (GetRoot()) os << *GetRoot();
}
