// Shortcut/separator items, their ordering, layout and icon drawing.

#pragma once

#include "Constants.h"
#include "DibBitmap.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lb {

enum class ItemKind : std::uint8_t { None = 0, Shortcut = 1, Separator = 2 };

struct Item final
{
    ItemKind kind = ItemKind::None;
    PIDLIST_ABSOLUTE pidl = nullptr;
    std::wstring fileName;
    std::wstring caption;
    RECT rect{};
    std::uint32_t hash = 0;
    bool needLoad = false;
    int loadAttempts = 0;
    double drawScale = 1.0;
    double targetScale = 1.0;
    // Per-item presentation overrides (profile section "[Items]").
    bool showText = true;
    bool hasTextColor = false;
    std::uint32_t textColor = 0;
    DibBitmap icon;
    bool shield = false;
    bool bitBucket = false;

    Item() = default;
    ~Item();
    Item(const Item&) = delete;
    Item& operator=(const Item&) = delete;
    Item(Item&&) noexcept;
    Item& operator=(Item&&) noexcept;

    [[nodiscard]] bool isSeparator() const noexcept { return kind == ItemKind::Separator; }
    [[nodiscard]] bool loadFromFile(const std::wstring& path);
    void loadIcon(int iconSize);
    [[nodiscard]] int getDropPart(const POINT& point, bool vertical, bool sideOnly) const noexcept;
};

[[nodiscard]] bool isSeparatorName(std::wstring_view name) noexcept;

struct PanelSizes
{
    SIZE button{};
    int separator = 0;
    int margin = 0;
};

class ItemList final
{
public:
    ItemList() = default;
    ~ItemList() = default;

    ItemList(const ItemList&) = delete;
    ItemList& operator=(const ItemList&) = delete;

    [[nodiscard]] auto begin() noexcept { return items_.begin(); }
    [[nodiscard]] auto end() noexcept { return items_.end(); }
    [[nodiscard]] auto begin() const noexcept { return items_.begin(); }
    [[nodiscard]] auto end() const noexcept { return items_.end(); }

    [[nodiscard]] std::size_t count() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] Item& at(std::size_t index) { return *items_[index]; }
    [[nodiscard]] const Item& at(std::size_t index) const { return *items_[index]; }
    [[nodiscard]] Item* findByHash(std::uint32_t hash) noexcept;

    void clear() noexcept;
    void add(std::unique_ptr<Item> item);
    void insert(std::size_t index, std::unique_ptr<Item> item);
    void erase(std::size_t index) noexcept;
    void move(std::size_t from, std::size_t to) noexcept;

    void loadFromListFile(const std::wstring& workDir, int iconSize, bool sortAlphabetically);
    void saveListFile(const std::wstring& workDir) const;

    void setIconSize(int size, int multiplier = 1);
    [[nodiscard]] int iconSize() const noexcept { return iconSize_; }
    [[nodiscard]] int iconLoadMultiplier() const noexcept { return loadMultiplier_; }

    void updateLines(bool vertical, int width, int height);
    [[nodiscard]] const std::vector<int>& lines() const noexcept { return lines_; }
    [[nodiscard]] int getLineIndex(int itemIndex) const noexcept;

    void sort();
    void draw(DibBitmap& target, const Item& item, int x, int y, int size = 0) const;
    void loadIcon(Item& item);
    void bitBucketUpdateIcon();

    PanelSizes sizes;

private:
    void quickSort(int left, int right);
    [[nodiscard]] bool sortCompare(int left, int right) const noexcept;

    std::vector<std::unique_ptr<Item>> items_;
    std::vector<int> lines_;
    int iconSize_ = 0;
    int loadMultiplier_ = 1;
    int lineWidth_ = 0;
    DibBitmap shield_;
};

} // namespace lb
