#include "Items.h"
#include "Common.h"
#include "Constants.h"
#include "Shell.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <utility>
#include <shellapi.h>
#include <shlwapi.h>

namespace lb {
namespace {

std::vector<std::wstring> readLines(const std::wstring& path)
{
    std::vector<std::wstring> result;
    std::vector<char> bytes = readFileBytes(path);
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(bytes.begin(), bytes.begin() + 3);
    }
    const std::wstring content = utf8ToWide(std::string_view(bytes.data(), bytes.size()));
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

// Any file or folder can be an item; only the panel's own bookkeeping and
// hidden/system entries are skipped.
bool isItemName(std::wstring_view name, DWORD attributes) noexcept
{
    if (name.empty() || name == L"." || name == L".." || name.front() == L'~')
    {
        return false;
    }
    if (iequals(name, LINKSLIST_FILE_NAME) || iequals(name, L"desktop.ini"))
    {
        return false;
    }
    if ((attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0)
    {
        return false;
    }
    return true;
}

std::vector<std::wstring> supportedFiles(const std::wstring& workDir)
{
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((workDir + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return result;
    }
    do
    {
        if (isItemName(data.cFileName, data.dwFileAttributes))
        {
            result.emplace_back(data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
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
}

Item::Item(Item&& other) noexcept
    : kind(other.kind), pidl(std::exchange(other.pidl, nullptr)),
      fileName(std::move(other.fileName)), caption(std::move(other.caption)),
      rect(other.rect), hash(other.hash), needLoad(other.needLoad),
      loadAttempts(other.loadAttempts), drawScale(other.drawScale),
      targetScale(other.targetScale), showText(other.showText),
      hasTextColor(other.hasTextColor), textColor(other.textColor),
      icon(std::move(other.icon)), shield(other.shield), bitBucket(other.bitBucket)
{
}

Item& Item::operator=(Item&& other) noexcept
{
    if (this != &other)
    {
        CoTaskMemFree(pidl);
        kind = other.kind;
        pidl = std::exchange(other.pidl, nullptr);
        fileName = std::move(other.fileName);
        caption = std::move(other.caption);
        rect = other.rect;
        hash = other.hash;
        needLoad = other.needLoad;
        loadAttempts = other.loadAttempts;
        drawScale = other.drawScale;
        targetScale = other.targetScale;
        showText = other.showText;
        hasTextColor = other.hasTextColor;
        textColor = other.textColor;
        icon = std::move(other.icon);
        shield = other.shield;
        bitBucket = other.bitBucket;
    }
    return *this;
}

bool Item::loadFromFile(const std::wstring& path)
{
    PIDLIST_ABSOLUTE parsed = nullptr;
    if (!parseDisplayName(path, &parsed))
    {
        return false;
    }
    CoTaskMemFree(pidl);
    pidl = parsed;
    fileName = path;
    hash = strHash(PathFindFileNameW(fileName.c_str()));
    caption = displayName(pidl);
    shield = checkShield(pidl);
    bitBucket = checkBitBucket(pidl);
    kind = ItemKind::Shortcut;
    return true;
}

void Item::loadIcon(int iconSize)
{
    icon = loadItemIcon(pidl, iconSize);
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

    // Keep the saved order for files that still exist, drop the rest and
    // append newly found shortcuts at the end.
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
        else if (item->loadFromFile(workDir + name))
        {
            items_.push_back(std::move(item));
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
        lines.emplace_back(PathFindFileNameW(item->fileName.c_str()));
    }
    writeLines(workDir + std::wstring(LINKSLIST_FILE_NAME), lines);
}

void ItemList::setIconSize(int size, int multiplier)
{
    multiplier = std::clamp(multiplier, 1, 4);
    if (iconSize_ != size || loadMultiplier_ != multiplier || shield_.handle() == nullptr)
    {
        iconSize_ = size;
        loadMultiplier_ = multiplier;
        shield_ = shieldOverlay(size * multiplier);
    }
    for (const auto& item : items_)
    {
        loadIcon(*item);
    }
}

void ItemList::loadIcon(Item& item)
{
    item.loadIcon(iconSize_ * loadMultiplier_);
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

void ItemList::draw(DibBitmap& target, const Item& item, int x, int y, int size) const
{
    const int targetSize = size > 0 ? size : iconSize_;
    if (item.icon.width() == 0)
    {
        return;
    }
    if (targetSize == item.icon.width())
    {
        target.compositeOver(item.icon, x, y);
        if (item.shield) target.compositeOver(shield_, x, y);
        return;
    }
    target.compositeOverScaled(item.icon, x, y, targetSize, targetSize);
    if (item.shield) target.compositeOverScaled(shield_, x, y, targetSize, targetSize);
}

} // namespace lb
