#include "expansion.hpp"
#include "../context.hpp"
#include "../raw_item.hpp"
#include "../cstring_item.hpp"
#include "../../sink.hpp"

namespace Neptools::Stcm
{

  void ExpansionItem::Header::Validate(FilePosition file_size) const
  {
#define VALIDATE(x) LIBSHIT_VALIDATE_FIELD("Stcm::ExpansionItemItem::Header", x)
    VALIDATE(name < file_size);
    VALIDATE(ptr == 0);
    for (const auto& p : pad) VALIDATE(p == 0);
#undef VALIDATE
  }

  ExpansionItem::ExpansionItem(Key k, Context& ctx, const Header& hdr)
    : Item{k, ctx}, name{Libshit::EmptyNotNull{}}
  {
    hdr.Validate(ctx.GetSize());

    index = hdr.index;
    name = ctx.GetLabelTo(hdr.name);
  }

  ExpansionItem& ExpansionItem::CreateAndInsert(ItemPointer ptr)
  {
    auto x = RawItem::Get<Header>(ptr);
    auto& ret = x.ritem.SplitCreate<ExpansionItem>(ptr.offset, x.t);

    MaybeCreate<CStringItem>(ret.name->GetPtr());

    return ret;
  }

  void ExpansionItem::Dump_(Sink& sink) const
  {
    Header hdr{};
    hdr.index = index;
    hdr.name = ToFilePos(name->GetPtr());
    sink.WriteGen(hdr);
  }

  void ExpansionItem::Inspect_(std::ostream& os, unsigned indent) const
  {
    Item::Inspect_(os, indent);
    os << "expansion(" << index << ", " << PrintLabel(name) << ")";
  }


  ExpansionsItem& ExpansionsItem::CreateAndInsert(
    ItemPointer ptr, uint32_t count)
  {
    auto& ret = ptr.AsChecked<RawItem>().SplitCreate<ExpansionsItem>(
      ptr.offset);
    ret.MoveNextToChild(count * sizeof(ExpansionItem::Header));
    auto it = ret.GetChildren().begin();

    for (uint32_t i = 0; i < count; ++i)
    {
      auto lbl = ret.GetContext()->CreateLabelFallback("expansion", *it);
      auto& exp = ExpansionItem::CreateAndInsert(lbl->GetPtr());
      it = std::next(exp.Iterator());
    }
    return ret;
  }

  void ExpansionsItem::Inspect_(std::ostream& os, unsigned indent) const
  {
    Item::Inspect_(os, indent);
    os << "expansions()";
    InspectChildren(os, indent);
  }

}

#include "expansion.binding.hpp"
