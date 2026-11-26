//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  PrimaryComponent.h
//
//  Base class for the primary components (carousel, grid and textlist).
//

#ifndef ES_CORE_COMPONENTS_PRIMARY_PRIMARY_COMPONENT_H
#define ES_CORE_COMPONENTS_PRIMARY_PRIMARY_COMPONENT_H

template <typename T> class PrimaryComponent : public virtual GuiComponent
{
public:
    enum class PrimaryType {
        CAROUSEL,
        GRID,
        TEXTLIST
    };

    enum class PrimaryAlignment {
        ALIGN_LEFT,
        ALIGN_CENTER,
        ALIGN_RIGHT
    };

    // IList functions.
    virtual bool isScrolling() const = 0;
    virtual void stopScrolling() = 0;
    virtual const int getScrollingVelocity() = 0;
    virtual void clear() = 0;
    virtual const T& getSelected() const = 0;
    virtual const T& getNext() const = 0;
    virtual const T& getPrevious() const = 0;
    virtual const T& getFirst() const = 0;
    virtual const T& getLast() const = 0;
    virtual bool setCursor(const T& obj) = 0;
    virtual bool remove(const T& obj) = 0;
    virtual int size() const = 0;

    // Functions used by all primary components.
    virtual void onShowPrimary() = 0;
    virtual void setCancelTransitionsCallback(const std::function<void()>& func) = 0;
    virtual void setCursorChangedCallback(const std::function<void(CursorState state)>& func) = 0;
    virtual int getCursor() = 0;
    virtual const size_t getNumEntries() = 0;
    virtual const bool getFadeAbovePrimary() const = 0;
    virtual const LetterCase getLetterCase() const = 0;
    virtual const LetterCase getLetterCaseAutoCollections() const = 0;
    virtual const LetterCase getLetterCaseCustomCollections() const = 0;
    virtual const bool getSystemNameSuffix() const = 0;
    virtual const LetterCase getLetterCaseSystemNameSuffix() const = 0;

    // Functions used by some primary components.
    virtual void onDemandTextureLoad() {}
    virtual void setAlignment(PrimaryAlignment align) {}

    // Mouse support.
    virtual bool setCursorIndex(int index) = 0;
    // Whether the wheel should map to left/right instead of up/down.
    virtual bool isHorizontal() const { return false; }
    // Entries per row, so one wheel notch moves one row.
    virtual int getColumns() const { return 1; }
    // Index of the entry drawn at a screen position in the last frame, or -1.
    int mouseHitTest(const glm::vec2& pos) const
    {
        // Later entries were drawn on top.
        for (auto it = mMouseHitBoxes.crbegin(); it != mMouseHitBoxes.crend(); ++it) {
            if (pos.x >= it->second.x && pos.y >= it->second.y && pos.x <= it->second.z &&
                pos.y <= it->second.w)
                return it->first;
        }
        return -1;
    }

protected:
    void addMouseHitBox(int index, const glm::mat4& trans, glm::vec2 pos, glm::vec2 size)
    {
        const glm::vec4 a {trans * glm::vec4 {pos.x, pos.y, 0.0f, 1.0f}};
        const glm::vec4 b {trans * glm::vec4 {pos.x + size.x, pos.y + size.y, 0.0f, 1.0f}};
        mMouseHitBoxes.emplace_back(index, glm::vec4 {std::min(a.x, b.x), std::min(a.y, b.y),
                                                      std::max(a.x, b.x), std::max(a.y, b.y)});
    }
    // Screen rectangles (x1, y1, x2, y2) of the entries drawn in the last frame.
    std::vector<std::pair<int, glm::vec4>> mMouseHitBoxes;
};

#endif // ES_CORE_COMPONENTS_PRIMARY_PRIMARY_COMPONENT_H
