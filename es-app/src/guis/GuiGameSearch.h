//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameSearch.h
//
//  Full-screen game search (osraj): on-screen keyboard for controllers, physical keyboard
//  typing, mouse. Searches the current console or all consoles, live results.
//

#ifndef ES_APP_GUIS_GUI_GAME_SEARCH_H
#define ES_APP_GUIS_GUI_GAME_SEARCH_H

#include "GuiComponent.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"

class FileData;
class SystemData;

class GuiGameSearch : public GuiComponent
{
public:
    // currentSystem is the console (or collection) the search was opened from.
    explicit GuiGameSearch(SystemData* currentSystem);
    ~GuiGameSearch();

    bool input(InputConfig* config, Input input) override;
    void textInput(const std::string& text, const bool pasting = false) override;
    bool mouseInput(const MouseEvent& event) override;
    void update(int deltaTime) override;
    void render(const glm::mat4& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

    // Ranks a display name against a query, 0 means no match. Exposed for tests.
    static int matchScore(const std::string& normName,
                          const std::string& initials,
                          const std::vector<std::string>& queryWords,
                          const std::string& normQuery);
    static std::string normalize(const std::string& text);

private:
    enum class Focus {
        KEYBOARD,
        RESULTS
    };

    enum class KeyAction {
        CHARACTER,
        SPACE,
        BACKSPACE,
        CLEAR
    };

    struct Key {
        std::string label;
        KeyAction action;
        int row;
        int col;
        int span;
        glm::vec4 rect; // x, y, w, h
        std::unique_ptr<TextComponent> text;
    };

    struct Entry {
        FileData* game;
        std::string name;
        std::string console;
        std::string normName;
        std::string initials;
    };

    struct Result {
        const Entry* entry;
        int score;
    };

    struct Row {
        std::unique_ptr<ImageComponent> cover;
        std::unique_ptr<TextComponent> name;
        std::unique_ptr<TextComponent> console;
        const Entry* shown {nullptr};
    };

    void buildLayout();
    void buildEntries();
    void runSearch();
    void setScopeAll(bool all);
    void pressKey(const Key& key);
    void backspace();
    void moveKeyCursor(int dx, int dy);
    void moveResultCursor(int amount);
    void navigate(int dx, int dy);
    void launchSelected();
    void goToSelected();
    void close();
    void updateRows();
    void updateStatus();
    int resultRowAt(const glm::vec2& pos) const;
    int keyAt(const glm::vec2& pos) const;
    static bool inside(const glm::vec4& rect, const glm::vec2& pos);

    Renderer* mRenderer;
    SystemData* mSystem;
    bool mScopeAll;
    bool mStartedTextInput;
    std::string mQuery;

    std::vector<Entry> mSystemEntries;
    std::vector<Entry> mAllEntries;
    std::vector<Result> mResults;

    Focus mFocus;
    int mKeyCursor;
    int mResultCursor;
    int mResultScroll;
    int mHoverKey;
    int mHoverRow;

    int mRepeatDx;
    int mRepeatDy;
    int mRepeatTimer;
    int mCaretTimer;

    std::vector<Key> mKeys;
    std::vector<Row> mRows;

    glm::vec4 mFieldRect;
    glm::vec4 mScopeThisRect;
    glm::vec4 mScopeAllRect;
    glm::vec4 mResultsRect;
    float mRowHeight;

    std::shared_ptr<Font> mFieldFont;
    std::shared_ptr<Font> mNameFont;
    std::shared_ptr<Font> mSmallFont;
    std::unique_ptr<ImageComponent> mMagnifier;
    std::unique_ptr<TextComponent> mFieldText;
    std::unique_ptr<TextComponent> mPlaceholderText;
    std::unique_ptr<TextComponent> mScopeThisText;
    std::unique_ptr<TextComponent> mScopeAllText;
    std::unique_ptr<TextComponent> mStatusText;
    std::unique_ptr<TextComponent> mEmptyText;
};

#endif // ES_APP_GUIS_GUI_GAME_SEARCH_H
