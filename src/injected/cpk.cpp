#include "cpk.hpp"
#include "hook.hpp"
#include "../fs.hpp"
#include "../except.hpp"

OperatorNewPtr operator_new;
OperatorDeletePtr operator_delete;

static CpkHandler::OpenFilePtr orig_open_file;
static CpkHandler::CloseFilePtr orig_close_file;
static CpkHandler::ReadPtr orig_read;

#include <iostream>
char CpkHandler::OpenFile(const char* fname, int* out)
{
    std::cout << "OpenFile " << basename << fname;
    fs::path pth{"kitfolder"};
    pth /= basename;
    pth /= fname;

    entry_vect.reserve(1000);
    if (!fs::exists(pth))
    {
        auto ret = (this->*orig_open_file)(fname, out);
        std::cout << " -> " << int(ret) << ", " << *out << std::endl;
        return ret;
    }

    std::cout << " -- hooking it" << std::endl;
    auto it = std::find_if(entry_vect.begin(), entry_vect.end(),
                           [](auto& x) { return x->is_valid == 0; });
    if (it == entry_vect.end())
    {
        entry_vect.reserve(entry_vect.size() + 1);
        auto ne =
            static_cast<CpkHandlerFileInfo*>(operator_new(sizeof(CpkHandlerFileInfo)));
        ne->huffmann_hdr = nullptr;
        ne->block = nullptr;
        ne->is_valid = false;
        entry_vect.push_back(ne);
        it = entry_vect.end() - 1;
    }

    auto h = CreateFileW(pth.c_str(), GENERIC_READ, FILE_SHARE_READ, 0,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (!h)
    {
        last_error = 3;
        return 0;
    }
    size_t size = GetFileSize(h, nullptr);

    // success
    (*it)->index = 0;
    (*it)->handle = h;
    (*it)->is_valid = true;
    (*it)->entry.field_000 = 0;
    (*it)->entry.file_index = 0;
    strncpy((*it)->entry.file_name, fname, 260);
    (*it)->entry.field_10c = 0;
    (*it)->entry.compressed_size = size;
    (*it)->entry.uncompressed_size = size;
    (*it)->entry.is_compressed = 0;
    (*it)->entry.field_11c = 0;
    (*it)->data_start = 0;
    (*it)->read_pos = 0;
    (*it)->decoded_block_index = -1;

    *out = it - entry_vect.begin();
    return 1;
}

char CpkHandler::CloseFile(unsigned index)
{
    std::cout << "CloseFile " << index << std::endl;
    return (this->*orig_close_file)(index);
}

char CpkHandler::Read(unsigned index, char* dst, int dst_size,
                      int* out_size_read)
{
    //std::cout << "Read " << index << " " << dst_size << std::endl;
    return (this->*orig_read)(index, dst, dst_size, out_size_read);
}

void CpkHandler::Hook()
{
    auto h = GetModuleHandleW(L"msvcr120.dll");
    if (!h) THROW(std::runtime_error{"Failed to load msvcr120.dll"});
    operator_new = reinterpret_cast<OperatorNewPtr>(
        GetProcAddress(h, "??2@YAPAXI@Z"));
    operator_delete = reinterpret_cast<OperatorDeletePtr>(
        GetProcAddress(h, "??3@YAXPAX@Z"));
    if (!operator_new || !operator_delete)
        THROW(std::runtime_error{"Failed to bind operator new/delete"});

    orig_open_file = ::Hook(image_base + 0x2ede90, &CpkHandler::OpenFile, 5);
    orig_close_file = ::Hook(image_base + 0x2ee1b0, &CpkHandler::CloseFile, 5);
    orig_read = ::Hook(image_base + 0x2ee630, &CpkHandler::Read, 5);
}
