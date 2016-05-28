#include "cl3.hpp"
#include "stcm/file.hpp"
#include "../except.hpp"
#include <fstream>
#include <boost/filesystem/operations.hpp>

namespace Neptools
{

void Cl3::Header::Validate(FilePosition file_size) const
{
#define VALIDATE(x) NEPTOOLS_VALIDATE_FIELD("Cl3::Header", x)
    VALIDATE(memcmp(magic, "CL3L", 4) == 0);
    VALIDATE(field_04 == 0);
    VALIDATE(field_08 == 3);
    VALIDATE(sections_offset + sections_count * sizeof(Section) <= file_size);
#undef VALIDATE
}

void Cl3::Section::Validate(FilePosition file_size) const
{
#define VALIDATE(x) NEPTOOLS_VALIDATE_FIELD("Cl3::Section", x)
    VALIDATE(name.is_valid());
    VALIDATE(data_offset <= file_size);
    VALIDATE(data_offset + data_size <= file_size);
    VALIDATE(field_2c == 0);
    VALIDATE(field_30 == 0 && field_34 == 0 && field_38 == 0 && field_3c == 0);
    VALIDATE(field_40 == 0 && field_44 == 0 && field_48 == 0 && field_4c == 0);
#undef VALIDATE
}

void Cl3::FileEntry::Validate(uint32_t block_size) const
{
#define VALIDATE(x) NEPTOOLS_VALIDATE_FIELD("Cl3::FileEntry", x)
    VALIDATE(name.is_valid());
    VALIDATE(data_offset <= block_size);
    VALIDATE(data_offset + data_size <= block_size);
    VALIDATE(field_214 == 0 && field_218 == 0 && field_21c == 0);
    VALIDATE(field_220 == 0 && field_224 == 0 && field_228 == 0 && field_22c == 0);
#undef VALIDATE
}

void Cl3::LinkEntry::Validate(uint32_t i, uint32_t file_count) const
{
#define VALIDATE(x) NEPTOOLS_VALIDATE_FIELD("Cl3::LinkEntry", x)
    VALIDATE(field_00 == 0);
    VALIDATE(linked_file_id < file_count);
    VALIDATE(link_id == i);
    VALIDATE(field_0c == 0);
    VALIDATE(field_10 == 0 && field_14 == 0 && field_18 == 0 && field_1c == 0);
#undef VALIDATE
}

Cl3::Cl3(Source src)
{
    AddInfo(&Cl3::Parse_, ADD_SOURCE(src), this, src);
}

void Cl3::Parse_(Source& src)
{
    src.CheckSize(sizeof(Header));
    auto hdr = src.Pread<Header>(0);
    hdr.Validate(src.GetSize());

    field_14 = hdr.field_14;

    src.Seek(hdr.sections_offset);
    uint32_t secs = hdr.sections_count;

    uint32_t file_offset = 0, file_count = 0, file_size,
        link_offset, link_count = 0;
    for (size_t i = 0; i < secs; ++i)
    {
        auto sec = src.Read<Section>();
        sec.Validate(src.GetSize());

        if (sec.name == "FILE_COLLECTION")
        {
            file_offset = sec.data_offset;
            file_count = sec.count;
            file_size = sec.data_size;
        }
        else if (sec.name == "FILE_LINK")
        {
            link_offset = sec.data_offset;
            link_count = sec.count;
            NEPTOOLS_VALIDATE_FIELD(
                "Cl3::Section",
                sec.data_size == link_count * sizeof(LinkEntry));
        }
    }

    src.Seek(file_offset);
    for (uint32_t i = 0; i < file_count; ++i)
    {
        auto e = src.Read<FileEntry>();
        e.Validate(file_size);

        entries.emplace_back(
            e.name.c_str(), e.field_200,
            std::make_unique<DumpableSource>(
                src, file_offset+e.data_offset, e.data_size));

        auto& ls = entries.back().links;
        uint32_t lbase = e.link_start;
        uint32_t lcount = e.link_count;
        for (uint32_t i = lbase; i < lbase+lcount; ++i)
        {
            auto le = src.Pread<LinkEntry>(link_offset + i*sizeof(LinkEntry));
            le.Validate(i - lbase, file_count);
            ls.push_back(le.linked_file_id);
        }
    }
}

static constexpr unsigned PAD_BYTES = 0x40;
static constexpr unsigned PAD = 0x3f;
void Cl3::Fixup()
{
    data_size = 0;
    link_count = 0;
    for (auto& e : entries)
    {
        e.src->Fixup();
        data_size += e.src->GetSize();
        data_size = (data_size + PAD) & ~PAD;
        link_count += e.links.size();
    }
}

FilePosition Cl3::GetSize() const
{
    FilePosition ret = (sizeof(Header)+PAD) & ~PAD;
    ret = (ret+sizeof(Section)*2+PAD) & ~PAD;
    ret = (ret+sizeof(FileEntry)*entries.size()+PAD) & ~PAD;
    ret += data_size+sizeof(LinkEntry)*link_count;
    return ret;
}

Cl3::Entry* Cl3::GetFile(const char* fname)
{
    for (auto& e : entries)
        if (e.name == fname)
            return &e;
    return nullptr;
}
Cl3::Entry* Cl3::GetFile(const std::string& fname)
{
    for (auto& e : entries)
        if (e.name == fname)
            return &e;
    return nullptr;
}

void Cl3::ExtractTo(const boost::filesystem::path& dir) const
{
    if (!boost::filesystem::is_directory(dir))
        boost::filesystem::create_directories(dir);

    for (const auto& e : entries)
    {
        auto sink = Sink::ToFile(dir / e.name.c_str(), e.src->GetSize());
        e.src->Dump(*sink);
    }
}

void Cl3::UpdateFromDir(const boost::filesystem::path& dir)
{
    for (auto& e : boost::filesystem::directory_iterator(dir))
        GetOrCreateFile(e.path().filename().string()).src =
            std::make_unique<DumpableSource>(Source::FromFile(e));

    for (size_t i = 0; i < entries.size(); )
        if (!boost::filesystem::exists(dir / entries[i].name))
            DeleteFile(i);
        else
            ++i;
}

void Cl3::DeleteFile(size_t i)
{
    entries.erase(entries.begin() + i);
    for (auto& e : entries)
        for (auto it = e.links.begin(); it != e.links.end(); )
        {
            if (*it == i)
            {
                it = e.links.erase(it);
                continue;
            }
            else if (*it > i)
                --*it;
            ++it;
        }
}

void Cl3::Inspect_(std::ostream& os) const
{
    os << "cl3(" << field_14 << ", files[\n";
    size_t i = 0;
    for (auto& e : entries)
    {
        os << "  [" << i++ << "] (";
        DumpBytes(os, e.name);
        os << ", " << e.field_200 << ", links[";
        bool first = true;
        for (auto& l : e.links)
        {
            if (!first) os << ", ";
            first = false;
            os << l;
        }
        os << "], ";
        e.src->Inspect(os);
        os << ")\n";
    }
}

void Cl3::Dump_(Sink& sink) const
{
    auto sections_offset = (sizeof(Header)+PAD) & ~PAD;
    auto files_offset = (sections_offset+sizeof(Section)*2+PAD) & ~PAD;
    auto data_offset = (files_offset+sizeof(FileEntry)*entries.size()+PAD) & ~PAD;
    auto link_offset = data_offset + data_size;

    Header hdr;
    memcpy(hdr.magic, "CL3L", 4);
    hdr.field_04 = 0;
    hdr.field_08 = 3;
    hdr.sections_count = 2;
    hdr.sections_offset = sections_offset;
    hdr.field_14 = field_14;
    sink.Write(hdr);
    sink.Pad(sections_offset-sizeof(Header));

    Section sec;
    memset(&sec, 0, sizeof(Section));
    sec.name = "FILE_COLLECTION";
    sec.count = entries.size();
    sec.data_size = link_offset - files_offset;
    sec.data_offset = files_offset;
    sink.Write(sec);

    sec.name = "FILE_LINK";
    sec.count = link_count;
    sec.data_size = link_count * sizeof(LinkEntry);
    sec.data_offset = link_offset;
    sink.Write(sec);
    sink.Pad((PAD_BYTES - ((2*sizeof(Section)) & PAD)) & PAD);

    FileEntry fe;
    fe.field_214 = fe.field_218 = fe.field_21c = 0;
    fe.field_220 = fe.field_224 = fe.field_228 = fe.field_22c = 0;

    // file entry header
    uint32_t offset = data_offset-files_offset, link_i = 0;
    for (auto& e : entries)
    {
        fe.name = e.name;
        fe.field_200 = e.field_200;
        fe.data_offset = offset;
        auto size = e.src->GetSize();
        fe.data_size = size;
        fe.link_start = link_i;
        fe.link_count = e.links.size();
        sink.Write(fe);

        offset = (offset+size+PAD) & ~PAD;
        link_i += e.links.size();
    }
    sink.Pad((PAD_BYTES - ((entries.size()*sizeof(FileEntry)) & PAD)) & PAD);

    // file data
    for (auto& e : entries)
    {
        e.src->Dump(sink);
        sink.Pad((PAD_BYTES - (e.src->GetSize() & PAD)) & PAD);
    }

    // links
    LinkEntry le;
    memset(&le, 0, sizeof(LinkEntry));
    for (auto& e : entries)
    {
        uint32_t i = 0;
        for (auto l : e.links)
        {
            le.linked_file_id = l;
            le.link_id = i++;
            sink.Write(le);
        }
    }
}

Stcm::File& Cl3::GetStcm()
{
    auto dat = GetFile("main.DAT");
    if (!dat) NEPTOOLS_THROW(DecodeError{"Invalid CL3 file: no main.DAT"});

    auto stcm = dynamic_cast<Stcm::File*>(dat->src.get());
    if (stcm) return *stcm;

    auto src = asserted_cast<DumpableSource*>(dat->src.get());
    auto nstcm = std::make_unique<Stcm::File>(*src);
    auto ret = nstcm.get();
    dat->src = std::move(nstcm);
    return *ret;
}

}
