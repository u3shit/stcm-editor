#include "../format/item.hpp"
#include "../format/cl3.hpp"
#include "../format/stcm/file.hpp"
#include "../format/stcm/gbnl.hpp"
#include "../except.hpp"
#include "../options.hpp"
#include "../utils.hpp"
#include "version.hpp"
#include <iostream>
#include <fstream>
#include <deque>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/exception/errinfo_file_name.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

using namespace Neptools;

namespace
{

struct State
{
    std::unique_ptr<Dumpable> file;
    Cl3* cl3;
    Stcm::File* stcm;
    Gbnl* gbnl;
};

State SmartOpen_(const boost::filesystem::path& fname)
{
    auto src = Source::FromFile(fname.native());
    if (src.GetSize() < 4)
        NEPTOOLS_THROW(DecodeError{"Input file too short"});

    char buf[4];
    src.Pread(0, buf, 4);
    if (memcmp(buf, "CL3L", 4) == 0)
    {
        auto cl3 = std::make_unique<Cl3>(src);
        auto ret2 = cl3.get();
        return {std::move(cl3), ret2, nullptr, nullptr};
    }
    else if (memcmp(buf, "STCM", 4) == 0)
    {
        auto stcm = std::make_unique<Stcm::File>(src);
        auto ret2 = stcm.get();
        return {std::move(stcm), nullptr, ret2, nullptr};
    }
    else if (src.GetSize() >= sizeof(Gbnl::Header) &&
             (memcmp(buf, "GSTL", 4) == 0 ||
              (src.Pread(src.GetSize() - sizeof(Gbnl::Header), buf, 4),
               memcmp(buf, "GBNL", 4) == 0)))
    {
        auto gbnl = std::make_unique<Gbnl>(src);
        auto ret2 = gbnl.get();
        return {std::move(gbnl), nullptr, nullptr, ret2};
    }
    else
        NEPTOOLS_THROW(DecodeError{"Unknown input file"});
}

State SmartOpen(const boost::filesystem::path& fname)
{
    return AddInfo(
        SmartOpen_,
        [&](auto& e) { e << boost::errinfo_file_name{fname.string()}; },
        fname);
}

template <typename T>
void ShellDump(const T* item, const char* name)
{
    std::unique_ptr<Sink> sink;
    if (name[0] == '-' && name[1] == '\0')
        sink = Sink::ToStdOut();
    else
        sink = Sink::ToFile(name, item->GetSize());
    item->Dump(*sink);
}

template <typename T, typename Fun>
void ShellInspectGen(const T* item, const char* name, Fun f)
{
    if (name[0] == '-' && name[1] == '\0')
        f(item, std::cout);
    else
        f(item, OpenOut(name));
}

template <typename T>
void ShellInspect(const T* item, const char* name)
{ ShellInspectGen(item, name, [](auto x, auto&& y) { y << *x; }); }

void EnsureStcm(State& st)
{
    if (st.stcm) return;
    if (!st.file) throw InvalidParam{"no file loaded"};
    if (!st.cl3)
        throw InvalidParam{"invalid file loaded: can't find STCM without CL3"};

    st.stcm = &st.cl3->GetStcm();
}

void EnsureGbnl(State& st)
{
    if (st.gbnl) return;
    EnsureStcm(st);
    st.gbnl = &st.stcm->FindGbnl();
}

bool auto_failed = false;
template <typename Pred, typename Fun>
void RecDo(const boost::filesystem::path& path, Pred p, Fun f, bool rec = false)
{
    if (p(path, rec))
    {
        try { f(path); }
        catch (const std::exception& e)
        {
            auto_failed = true;
            std::cerr << "Failed: ";
            PrintException(std::cerr);
        }
    }
    else if (boost::filesystem::is_directory(path))
        for (auto& e: boost::filesystem::directory_iterator(path))
            RecDo(e, p, f, true);
    else if (!rec)
        std::cerr << "Invalid filename: " << path << std::endl;
}

enum class Mode
{
#define MODE_PARS(X)                                                            \
    X(AUTO_STRTOOL,   "auto-strtool",   "import/export .cl3/.gbin/.gstr texts") \
    X(EXPORT_STRTOOL, "export-strtool", "export .cl3/.gbin/.gstr to .txt")      \
    X(IMPORT_STRTOOL, "import-strtool", "import .cl3/.gbin/.gstr from .txt")    \
    X(AUTO_CL3,       "auto-cl3",       "unpack/pack .cl3 files")               \
    X(UNPACK_CL3,     "unpack-cl3",     "unpack .cl3 files")                    \
    X(PACK_CL3,       "pack-cl3",       "pack .cl3 files")                      \
    X(MANUAL,         "manual",         "manual processing (set automatically)")
#define GEN_ENUM(name, shit1, shit2) name,
    MODE_PARS(GEN_ENUM)
#undef GEN_ENUM
} mode = Mode::AUTO_STRTOOL;

void DoAutoFun(const boost::filesystem::path& p)
{
    boost::filesystem::path cl3, txt;
    bool import;
    if (boost::ends_with(p.native(), ".txt"))
    {
        cl3 = p.native().substr(0, p.native().size()-4);
        txt = p;
        import = true;
        std::cerr << "Importing: " << cl3 << " <- " << txt << std::endl;
    }
    else
    {
        cl3 = txt = p;
        txt += ".txt";
        import = false;
        std::cerr << "Exporting: " << cl3 << " -> " << txt << std::endl;
    }

    auto st = SmartOpen(cl3);
    EnsureGbnl(st);
    if (import)
    {
        st.gbnl->ReadTxt(OpenIn(txt));
        if (st.stcm) st.stcm->Fixup();
        st.file->Fixup();
        st.file->Dump(cl3);
    }
    else
        st.gbnl->WriteTxt(OpenOut(txt));
}

void DoAutoCl3(const boost::filesystem::path& p)
{
    if (boost::filesystem::is_directory(p))
    {
        boost::filesystem::path cl3_file =
            p.native().substr(0, p.native().size() - 4);
        std::cerr << "Packing " << cl3_file << std::endl;
        Cl3 cl3{Source::FromFile(cl3_file)};
        cl3.UpdateFromDir(p);
        cl3.Fixup();
        cl3.Dump(cl3_file);
    }
    else
    {
        std::cerr << "Extracting " << p << std::endl;
        Cl3 cl3{Source::FromFile(p)};
        auto out = p;
        cl3.ExtractTo(out += ".out");
    }
}

inline bool is_file(const boost::filesystem::path& pth)
{
    auto stat = boost::filesystem::status(pth);
    return boost::filesystem::is_regular_file(stat) ||
        boost::filesystem::is_symlink(stat);
}

bool IsBin(const boost::filesystem::path& p, bool = false)
{
    return is_file(p) && (
        boost::ends_with(p.native(), ".cl3") ||
        boost::ends_with(p.native(), ".gbin") ||
        boost::ends_with(p.native(), ".gstr"));
}

bool IsTxt(const boost::filesystem::path& p, bool = false)
{
    return is_file(p) && (
        boost::ends_with(p.native(), ".cl3.txt") ||
        boost::ends_with(p.native(), ".gbin.txt") ||
        boost::ends_with(p.native(), ".gstr.txt"));
}

bool IsCl3(const boost::filesystem::path& p, bool = false)
{
    return is_file(p) && boost::ends_with(p.native(), ".cl3");
}

bool IsCl3Dir(const boost::filesystem::path& p, bool = false)
{
    return boost::filesystem::is_directory(p) &&
        boost::ends_with(p.native(), ".cl3.out");
}

void DoAuto(const boost::filesystem::path& path)
{
    bool (*pred)(const boost::filesystem::path&, bool);
    void (*fun)(const boost::filesystem::path& p);

    switch (mode)
    {
    case Mode::AUTO_STRTOOL:
        pred = [](auto& p, bool rec)
        {
            if (rec)
                return (IsTxt(p) &&
                        boost::filesystem::exists(
                            p.native().substr(0, p.native().size()-4))) ||
                       (IsBin(p) &&
                        !boost::filesystem::exists(
                            boost::filesystem::path(p)+=".txt"));
            else
                return IsBin(p) || IsTxt(p);
        };
        fun = DoAutoFun;
        break;

    case Mode::EXPORT_STRTOOL:
        pred = IsBin;
        fun = DoAutoFun;
        break;
    case Mode::IMPORT_STRTOOL:
        pred = IsTxt;
        fun = DoAutoFun;
        break;

    case Mode::AUTO_CL3:
        pred = [](auto& p, bool rec)
        {
            if (rec)
                return IsCl3Dir(p) ||
                    (IsCl3(p) &&
                     !boost::filesystem::exists(
                         boost::filesystem::path(p)+=".out"));
            else
                return IsCl3(p) || IsCl3Dir(p);
        };
        fun = DoAutoCl3;
        break;

    case Mode::UNPACK_CL3:
        pred = IsCl3;
        fun = DoAutoCl3;
        break;
    case Mode::PACK_CL3:
        pred = IsCl3Dir;
        fun = DoAutoCl3;
        break;

    case Mode::MANUAL:
        throw InvalidParam{"Can't use auto files in manual mode"};
    }
    RecDo(path, pred, fun);
}

}

int main(int argc, char** argv)
{
    State st;
    auto& parser = OptionParser::GetGlobal();
    OptionGroup hgrp{parser, "High-level options"};
    OptionGroup lgrp{parser, "Low-level options", "See README for details"};

    Option mode_opt{
        hgrp, "mode", 'm', 1, "OPTION",
#define GEN_HELP(_, key, help) "\t\t" key ": " help "\n"
        "Set operating mode:\n" MODE_PARS(GEN_HELP),
#undef GEN_HELP
        [](auto&& args)
        {
            if (0);
#define GEN_IFS(c, str, _) else if (strcmp(args.front(), str) == 0) mode = Mode::c;
            MODE_PARS(GEN_IFS)
#undef GEN_IFS
            else throw InvalidParam{"invalid argument"};
        }};

    Option open_opt{
        lgrp, "open", 1, "FILE", "Opens FILE as cl3 or stcm file",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            st = SmartOpen(args.front());
        }};
    Option save_opt{
        lgrp, "save", 1, "FILE|-", "Saves the loaded file to FILE or stdout",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.file) throw InvalidParam{"no file loaded"};
            st.file->Fixup();
            ShellDump(st.file.get(), args.front());
        }};
    Option create_cl3_opt{
        lgrp, "create-cl3", 0, nullptr, "Creates an empty cl3 file",
        [&](auto&&)
        {
            mode = Mode::MANUAL;
            auto c = std::make_unique<Cl3>();
            auto c2 = c.get();
            st = {std::move(c), c2, nullptr, nullptr};
        }};
    Option list_files_opt{
        lgrp, "list-files", 0, nullptr, "Lists the contents of the cl3 archive\n",
        [&](auto&&)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            size_t i = 0;
            for (const auto& e : st.cl3->entries)
            {
                std::cout << i++ << '\t' << e.name << '\t' << e.src->GetSize()
                          << "\tlinks:";
                for (auto l : e.links) std::cout << ' ' << l;
                std::cout << std::endl;
            }
        }};
    Option extract_file_opt{
        lgrp, "extract-file", 2, "NAME OUT_FILE|-",
        "Extract NAME from cl3 archive to OUT_FILE or stdout",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            auto e = st.cl3->GetFile(args[0]);

            if (!e)
                throw InvalidParam{"specified file not found"};
            else
                ShellDump(e->src.get(), args[1]);
        }};
    Option extract_files_opt{
        lgrp, "extract-files", 1, "DIR", "Extract the cl3 archive to DIR",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            st.cl3->ExtractTo(args.front());
        }};
    Option replace_file_opt{
        lgrp, "replace-file", 2, "NAME IN_FILE",
        "Adds or replaces NAME in cl3 archive with IN_FILE",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};

            auto& e = st.cl3->GetOrCreateFile(args[0]);
            e.src = std::make_unique<DumpableSource>(Source::FromFile(args[1]));
        }};
    Option remove_file_opt{
        lgrp, "remove-file", 1, "NAME", "Removes NAME from cl3 archive",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            auto e = st.cl3->GetFile(args.front());
            if (!e)
                throw InvalidParam{"specified file not found"};
            else
                st.cl3->DeleteFile(*e);
        }};
    Option set_link_opt{
        lgrp, "set-link", 3, "NAME ID DEST", "Sets link at NAME, ID to DEST",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            auto e = st.cl3->GetFile(args[0]);
            auto i = std::stoul(args[1]);
            auto e2 = st.cl3->GetFile(args[2]);
            if (!e || !e2) throw InvalidParam{"specified file not found"};

            if (i < e->links.size())
                e->links[i] = e2 - &st.cl3->entries.front();
            else if (i == e->links.size())
                e->links.push_back(e2 - &st.cl3->entries.front());
            else
                throw InvalidParam{"invalid link id"};
        }};
    Option remove_link_opt{
        lgrp, "remove-link", 2, "NAME ID", "Remove link ID from NAME",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.cl3) throw InvalidParam{"no cl3 loaded"};
            auto e = st.cl3->GetFile(args[0]);
            auto i = std::stoul(args[1]);
            if (!e) throw InvalidParam{"specified file not found"};

            if (i < e->links.size())
                e->links.erase(e->links.begin() + i);
            else
                throw InvalidParam{"invalid link id"};
        }};
    Option inspect_opt{
        lgrp, "inspect", 1, "OUT|-",
        "Inspects currently loaded file into OUT or stdout",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            if (!st.file) throw InvalidParam{"No file loaded"};
            ShellInspect(st.file.get(), args.front());
        }};
    Option inspect_stcm_opt{
        lgrp, "inspect-stcm", 1, "OUT|-",
        "Inspects only the stcm portion of the currently loaded file into OUT or stdout",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            EnsureStcm(st);
            ShellInspect(st.stcm, args.front());
        }};
    Option parse_stcmp_opt{
        lgrp, "parse-stcm", 0, nullptr,
        "Parse STCM-inside-CL3 (usually done automatically)",
        [&](auto&&)
        {
            mode = Mode::MANUAL;
            EnsureStcm(st);
        }};

    Option export_txt_opt{
        lgrp, "export-txt", 1, "OUT_FILE|-", "Export text to OUT_FILE or stdout",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            EnsureGbnl(st);
            ShellInspectGen(st.gbnl, args.front(),
                            [](auto& x, auto&& y) { x->WriteTxt(y); });
        }};
    Option import_txt_opt{
        lgrp, "import-txt", 1, "IN_FILE|-", "Read text from IN_FILE or stdin",
        [&](auto&& args)
        {
            mode = Mode::MANUAL;
            EnsureGbnl(st);
            auto fname = args.front();
            if (fname[0] == '-' && fname[1] == '\0')
                st.gbnl->ReadTxt(std::cin);
            else
                st.gbnl->ReadTxt(OpenIn(fname));
            if (st.stcm) st.stcm->Fixup();
        }};

    boost::filesystem::path self{argv[0]};
    if (boost::iequals(self.filename().string(), "cl3-tool")
#ifdef WINDOWS
        || boost::iequals(self.filename().string(), "cl3-tool.exe")
#endif
        )
        mode = Mode::AUTO_CL3;

    parser.SetVersion("NepTools stcm-editor v" NEPTOOLS_VERSION);
    parser.SetUsage("[--options] [<file/directory>...]");
    parser.SetShowHelpOnNoOptions();
    parser.SetNoArgHandler(DoAuto);

    try { parser.Run(argc, argv); }
    catch (const Exit& e) { return !e.success; }
    catch (...)
    {
        std::cerr << "Fatal error, aborting\n";
        PrintException(std::cerr);
        return 2;
    }
    return auto_failed;
}
