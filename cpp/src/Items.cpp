#include "Items.h"
#include "Common.h"
#include "Constants.h"
#include "Shell.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <shellapi.h>
#include <shlwapi.h>

namespace lb {
namespace {

std::vector<std::wstring> readLines(const std::wstring& path)
{
    std::vector<std::wstring> result;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return result;
    }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }
    const std::wstring content = utf8ToWide(bytes);
    std::size_t pos = 0;
    while (pos < content.size())
    {
        std::size_t end = content.find(L'\n', pos);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }
        std::wstring line(content, pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        result.push_back(std::move(line));
    }
    return result;
}

void writeLines(const std::wstring& path, const std::vector<std::wstring>& lines)
{
    std::string bytes = "\xEF\xBB\xBF";
    for (const std::wstring& line : lines)
    {
        bytes += wideToUtf8(line);
        bytes += "\r\n";
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

std::vector<std::wstring> supportedFiles(const std::wstring& workDir)
{
    std::vector<std::wstring> result;
    for (const auto extension : ES_ARRAY)
    {
        const std::wstring pattern = workDir + L"*" + std::wstring(extension);
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            continue;
        }
        do
        {
            result.emplace_back(data.cFileName);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::sort(result.begin(), result.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                    b.c_str(), static_cast<int>(b.size()), TRUE) < CSTR_EQUAL;
    });
    return result;
}

} // namespace

Item::~Item()
{
    CoTaskMemFree(pidl);
    DeleteObject(bitmap);
}

Item::Item(Item&& other) noexcept
    : kind(other.kind), pidl(other.pidl), fileName(std::move(other.fileName)),
      caption(std::move(other.caption)), rect(other.rect), hash(other.hash),
      needLoad(other.needLoad), bitmap(other.bitmap), shield(other.shield),
      bitBucket(other.bitBucket)
{
    other.pidl = nullptr;
    other.bitmap = nullptr;
}

Item& Item::operator=(Item&& other) noexcept
{
    if (this != &other)
    {
        CoTaskMemFree(pidl);
        DeleteObject(bitmap);
        kind = other.kind;
        pidl = other.pidl;
        fileName = std::move(other.fileName);
        caption = std::move(other.caption);
        rect = other.rect;
        hash = other.hash;
        needLoad = other.needLoad;
        bitmap = other.bitmap;
        shield = other.shield;
        bitBucket = other.bitBucket;
        other.pidl = nullptr;
        other.bitmap = nullptr;
    }
    return *this;
}

bool Item::loadFromFile(const std::wstring& path)
{
    CoTaskMemFree(pidl);
    pidl = nullptr;
    if (!parseDisplayName(path, &pidl))
    {
        return false;
    }
    fileName = path;
    hash = strHash(PathFindFileNameW(const_cast<wchar_t*>(path.c_str())));
    caption = displayName(pidl);
    shield = checkShield(pidl);
    bitBucket = checkBitBucket(pidl);
    return true;
}

void Item::loadIcon(int iconSize)
{
    DeleteObject(bitmap);
    bitmap = loadIconFromPidl(pidl, iconSize);
}

int Item::getDropPart(const POINT& point, bool vertical, bool sideOnly) const noexcept
{
    if (sideOnly)
    {
        const POINT center = centerPoint(rect);
        return vertical ? (point.y < center.y ? -1 : 1) : (point.x < center.x ? -1 : 1);
    }
    if (vertical)
    {
        const int part0 = (rectHeight(rect) / 3) * 2;
        const int part1 = (rectHeight(rect) - part0) / 2;
        if (point.y < rect.top + part1) return -1;
        if (point.y >= rect.top + part1 + part0) return 1;
    }
    else
    {
        const int part0 = (rectWidth(rect) / 3) * 2;
        const int part1 = (rectWidth(rect) - part0) / 2;
        if (point.x < rect.left + part1) return -1;
        if (point.x >= rect.left + part1 + part0) return 1;
    }
    return 0;
}

bool isSeparatorName(std::wstring_view name) noexcept
{
    return name == L"|";
}

Item* ItemList::findByHash(std::uint32_t hash) noexcept
{
    for (const auto& item : items_)
    {
        if (item->hash == hash)
        {
            return item.get();
        }
    }
    return nullptr;
}

void ItemList::clear() noexcept
{
    items_.clear();
    lines_.clear();
}

void ItemList::add(std::unique_ptr<Item> item)
{
    items_.push_back(std::move(item));
}

void ItemList::insert(std::size_t index, std::unique_ptr<Item> item)
{
    index = std::min(index, items_.size());
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
}

void ItemList::erase(std::size_t index) noexcept
{
    if (index < items_.size())
    {
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void ItemList::move(std::size_t from, std::size_t to) noexcept
{
    if (from >= items_.size() || to >= items_.size())
    {
        return;
    }
    auto item = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
}

void ItemList::loadFromListFile(const std::wstring& workDir, int iconSize, bool sortAlphabetically)
{
    clear();
    std::vector<std::wstring> order = readLines(workDir + std::wstring(LINKSLIST_FILE_NAME));
    std::vector<std::wstring> found = supportedFiles(workDir);

    std::size_t i = 0;
    while (i < order.size())
    {
        if (isSeparatorName(order[i]))
        {
            ++i;
            continue;
        }
        const auto it = std::find_if(found.begin(), found.end(),
            [&](const std::wstring& name) { return iequals(name, order[i]); });
        if (it != found.end())
        {
            found.erase(it);
            ++i;
        }
        else
        {
            order.erase(order.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    order.insert(order.end(), found.begin(), found.end());

    items_.reserve(order.size());
    for (const std::wstring& name : order)
    {
        auto item = std::make_unique<Item>();
        if (isSeparatorName(name))
        {
            item->kind = ItemKind::Separator;
            item->caption = L"|";
            item->fileName = L"|";
            items_.push_back(std::move(item));
        }
        else
        {
            if (item->loadFromFile(workDir + name))
            {
                items_.push_back(std::move(item));
            }
        }
    }
    if (sortAlphabetically)
    {
        sort();
    }
    setIconSize(iconSize);
}

void ItemList::saveListFile(const std::wstring& workDir) const
{
    std::vector<std::wstring> lines;
    lines.reserve(items_.size());
    for (const auto& item : items_)
    {
        const wchar_t* name = PathFindFileNameW(const_cast<wchar_t*>(item->fileName.c_str()));
        lines.emplace_back(name);
    }
    writeLines(workDir + std::wstring(LINKSLIST_FILE_NAME), lines);
}

void ItemList::setIconSize(int size)
{
    if (iconSize_ == size)
    {
        return;
    }
    iconSize_ = size;

    SHSTOCKICONINFO info{};
    info.cbSize = sizeof(info);
    DeleteObject(shieldBitmap_);
    shieldBitmap_ = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_SHIELD, SHGSI_ICONLOCATION, &info)))
    {
        shieldBitmap_ = shieldOverlay(dc, info.szPath, info.iIcon, size);
    }
    DeleteDC(dc);
    for (const auto& item : items_)
    {
        loadIcon(*item);
    }
}

void ItemList::loadIcon(Item& item)
{
    item.loadIcon(iconSize_);
}

void ItemList::bitBucketUpdateIcon()
{
    for (const auto& item : items_)
    {
        if (item->bitBucket)
        {
            loadIcon(*item);
        }
    }
}

void ItemList::updateLines(bool vertical, int width, int height)
{
    for (const auto& item : items_)
    {
        item->rect = RECT{};
        if (item->isSeparator())
        {
            item->rect.right = sizes.button.cx;
            item->rect.bottom = vertical ? sizes.separator : sizes.button.cy;
        }
        else
        {
            item->rect.right = sizes.button.cx;
            item->rect.bottom = sizes.button.cy;
        }
    }

    const int margin = sizes.margin;
    int x = vertical ? 0 : margin;
    int y = vertical ? margin : 0;
    int count = 0;
    lines_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        ++count;
        RECT rect = items_[i]->rect;
        OffsetRect(&rect, x, y);
        if (vertical)
        {
            y += rectHeight(rect);
            if (i + 1 < items_.size() && (y + rectHeight(items_[i + 1]->rect)) > height)
            {
                y = margin;
                x += rectWidth(rect);
                lines_.push_back(count);
                count = 0;
            }
        }
        else
        {
            x += rectWidth(rect);
            if (i + 1 < items_.size() && (x + rectWidth(items_[i + 1]->rect)) > width)
            {
                x = margin;
                y += rectHeight(rect);
                lines_.push_back(count);
                count = 0;
            }
        }
        items_[i]->rect = rect;
    }
    if (count > 0)
    {
        lines_.push_back(count);
    }
    if (lines_.empty())
    {
        lines_.push_back(0);
    }

    if (lines_[0] == 0)
    {
        lineWidth_ = 0;
    }
    else if (vertical)
    {
        lineWidth_ = items_[static_cast<std::size_t>(lines_[0]) - 1]->rect.bottom - items_[0]->rect.top;
    }
    else
    {
        lineWidth_ = items_[static_cast<std::size_t>(lines_[0]) - 1]->rect.right - items_[0]->rect.left;
    }

    if (globals().layout == PanelLayout::Center && lines_.size() == 1)
    {
        const int available = vertical ? height : width;
        const int offset = (available - lineWidth_) / 2 - margin;
        for (const auto& item : items_)
        {
            if (vertical)
            {
                OffsetRect(&item->rect, 0, offset);
            }
            else
            {
                OffsetRect(&item->rect, offset, 0);
            }
        }
    }
}

int ItemList::getLineIndex(int itemIndex) const noexcept
{
    int sum = 0;
    for (std::size_t i = 0; i < lines_.size(); ++i)
    {
        sum += lines_[i];
        if (itemIndex < sum)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

bool ItemList::sortCompare(int left, int right) const noexcept
{
    return StrCmpLogicalW(items_[static_cast<std::size_t>(left)]->caption.c_str(),
                          items_[static_cast<std::size_t>(right)]->caption.c_str()) < 0;
}

void ItemList::quickSort(int left, int right)
{
    if (left >= right)
    {
        return;
    }
    int i = left;
    int j = right;
    int pivot = (left + right) >> 1;
    do
    {
        i = left;
        j = right;
        while (sortCompare(i, pivot)) ++i;
        while (sortCompare(pivot, j)) --j;
        if (i <= j)
        {
            if (i != j)
            {
                std::swap(items_[static_cast<std::size_t>(i)], items_[static_cast<std::size_t>(j)]);
            }
            if (pivot == i) pivot = j;
            else if (pivot == j) pivot = i;
            ++i;
            --j;
        }
    } while (i <= j);
    if (left < j)
    {
        quickSort(left, j);
    }
    if (i < right)
    {
        quickSort(i, right);
    }
}

void ItemList::sort()
{
    std::vector<int> separators;
    separators.push_back(-1);
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i]->isSeparator())
        {
            separators.push_back(static_cast<int>(i));
        }
    }
    separators.push_back(static_cast<int>(items_.size()));
    for (std::size_t i = 0; i + 1 < separators.size(); ++i)
    {
        const int l = separators[i] + 1;
        const int r = separators[i + 1] - 1;
        if (r - l >= 2)
        {
            quickSort(l, r);
        }
    }
}

void ItemList::draw(HDC dc, const Item& item, int x, int y)
{
    const BLENDFUNCTION blend = premultipliedBlend();
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(source, item.bitmap);
    if (item.bitmap != nullptr)
    {
        AlphaBlend(dc, x, y, iconSize_, iconSize_, source, 0, 0, iconSize_, iconSize_, blend);
    }
    if (item.shield && shieldBitmap_ != nullptr)
    {
        SelectObject(source, shieldBitmap_);
        AlphaBlend(dc, x, y, iconSize_, iconSize_, source, 0, 0, iconSize_, iconSize_, blend);
    }
    SelectObject(source, previous);
    DeleteDC(source);
}

} // namespace lb
