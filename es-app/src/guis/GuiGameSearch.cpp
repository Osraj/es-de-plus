//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameSearch.cpp
//
//  Full-screen game search (osraj): on-screen keyboard for controllers, physical keyboard
//  typing, mouse. Searches the current console or all consoles, live results.
//

#include "guis/GuiGameSearch.h"

#include "FileData.h"
#include "Settings.h"
#include "SystemData.h"
#include "UIModeController.h"
#include "Window.h"
#include "resources/Font.h"
#include "utils/StringUtil.h"
#include "views/GamelistView.h"
#include "views/ViewController.h"

#include <SDL2/SDL.h>
#include <algorithm>

namespace
{
    constexpr unsigned int ACCENT {0x22D3EEFF};
    constexpr unsigned int DARK_TEXT {0x0A0C12FF};
    constexpr unsigned int TEXT {0xFFFFFFFF};
    constexpr unsigned int DIM_TEXT {0xFFFFFF99};
    constexpr int VISIBLE_ROWS {8};
    constexpr int REPEAT_DELAY {400};
    constexpr int REPEAT_INTERVAL {70};
    constexpr size_t MAX_QUERY {60};

    // Keyboard rows; the last row holds the wide keys.
    const std::vector<std::string> KEY_ROWS {"1234567890", "qwertyuiop", "asdfghjkl'",
                                             "zxcvbnm-:&"};
} // namespace

GuiGameSearch::GuiGameSearch(SystemData* currentSystem)
    : mRenderer {Renderer::getInstance()}
    , mSystem {currentSystem}
    , mScopeAll {false}
    , mStartedTextInput {false}
    , mFocus {Focus::KEYBOARD}
    , mKeyCursor {0}
    , mResultCursor {0}
    , mResultScroll {0}
    , mHoverKey {-1}
    , mHoverRow {-1}
    , mRepeatDx {0}
    , mRepeatDy {0}
    , mRepeatTimer {0}
    , mCaretTimer {0}
    , mRowHeight {0.0f}
{
    setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());

    buildEntries();
    // The current console is the default scope. Tiles that are not really consoles (a tool
    // entry, a single game) default to all consoles, as searching them is pointless.
    mScopeAll = mSystem == nullptr || mSystemEntries.size() < 2;

    buildLayout();
    runSearch();

    if (!SDL_IsTextInputActive()) {
        SDL_StartTextInput();
        mStartedTextInput = true;
    }

    ViewController::getInstance()->pauseViewVideos();
}

GuiGameSearch::~GuiGameSearch()
{
    if (mStartedTextInput)
        SDL_StopTextInput();
}

std::string GuiGameSearch::normalize(const std::string& text)
{
    // Lower case, punctuation to spaces, apostrophes dropped ("Assassin's" -> "assassins"),
    // "&" spelled out, single spaces.
    const std::string lower {Utils::String::toLower(text)};
    std::string out;
    out.reserve(lower.size());
    for (const char c : lower) {
        const unsigned char u {static_cast<unsigned char>(c)};
        if (c == '\'')
            continue;
        if (c == '&')
            out.append(" and ");
        else if (u >= 0x80 || std::isalnum(u))
            out.push_back(c);
        else
            out.push_back(' ');
    }
    std::string collapsed;
    collapsed.reserve(out.size());
    for (const char c : out) {
        if (c == ' ' && (collapsed.empty() || collapsed.back() == ' '))
            continue;
        collapsed.push_back(c);
    }
    while (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    return collapsed;
}

int GuiGameSearch::matchScore(const std::string& normName,
                              const std::string& initials,
                              const std::vector<std::string>& queryWords,
                              const std::string& normQuery)
{
    if (queryWords.empty())
        return 1;

    int score {0};
    for (const std::string& word : queryWords) {
        const size_t pos {normName.find(word)};
        if (pos == std::string::npos) {
            // "gta" finds "Grand Theft Auto", "mgs" finds "Metal Gear Solid" (title start only,
            // anywhere else two letters match far too much).
            if (word.size() >= 2 && initials.compare(0, word.size(), word) == 0)
                score += 1;
            else
                return 0;
        }
        else if (pos == 0 || normName[pos - 1] == ' ') {
            score += 3; // Start of a word.
        }
        else {
            score += 1;
        }
    }
    if (normName == normQuery)
        score += 20;
    else if (normName.compare(0, normQuery.size(), normQuery) == 0)
        score += 10;
    return score;
}

void GuiGameSearch::buildEntries()
{
    const bool showHidden {Settings::getInstance()->getBool("ShowHiddenGames")};
    const bool kidMode {UIModeController::getInstance()->isUIModeKid()};

    auto collect = [showHidden, kidMode](SystemData* system, std::vector<Entry>& out) {
        for (FileData* game : system->getRootFolder()->getFilesRecursive(GAME, false)) {
            if (!showHidden && game->getHidden())
                continue;
            if (kidMode && !game->getKidgame())
                continue;
            Entry entry;
            entry.game = game;
            entry.name = game->getName();
            entry.console = game->getSourceFileData()->getSystem()->getFullName();
            entry.normName = normalize(entry.name);
            for (const std::string& word :
                 Utils::String::delimitedStringToVector(entry.normName, " ")) {
                if (!word.empty())
                    entry.initials.push_back(word.front());
            }
            out.emplace_back(std::move(entry));
        }
    };

    for (SystemData* system : SystemData::sSystemVector) {
        if (system->isGameSystem() && !system->isCollection())
            collect(system, mAllEntries);
    }
    if (mSystem != nullptr)
        collect(mSystem, mSystemEntries);

    auto byName = [](const Entry& a, const Entry& b) { return a.normName < b.normName; };
    std::stable_sort(mAllEntries.begin(), mAllEntries.end(), byName);
    std::stable_sort(mSystemEntries.begin(), mSystemEntries.end(), byName);
}

void GuiGameSearch::buildLayout()
{
    const float w {mSize.x};
    const float h {mSize.y};

    mFieldFont = Font::get(0.036f * h);
    mNameFont = Font::get(0.030f * h);
    mSmallFont = Font::get(0.024f * h, FONT_PATH_LIGHT);

    // Starts below the clock/battery/wifi icons that ES-DE draws over every screen.
    mFieldRect = {0.05f * w, 0.09f * h, 0.60f * w, 0.075f * h};
    mScopeThisRect = {0.67f * w, 0.09f * h, 0.14f * w, 0.075f * h};
    mScopeAllRect = {0.81f * w, 0.09f * h, 0.14f * w, 0.075f * h};
    mResultsRect = {0.47f * w, 0.23f * h, 0.48f * w, 0.68f * h};
    mRowHeight = mResultsRect.w / static_cast<float>(VISIBLE_ROWS);

    const float iconSize {mFieldRect.w * 0.45f};
    mMagnifier = std::make_unique<ImageComponent>(false, false);
    mMagnifier->setResize(iconSize, iconSize);
    mMagnifier->setImage(":/graphics/search.svg");
    mMagnifier->setPosition(mFieldRect.x + mFieldRect.w * 0.45f,
                            mFieldRect.y + (mFieldRect.w - iconSize) * 0.5f);
    const glm::vec3 textPos {mFieldRect.x + mFieldRect.w * 0.5f + iconSize, mFieldRect.y, 0.0f};
    const glm::vec2 textSize {mFieldRect.z - mFieldRect.w * 0.5f - iconSize * 2.0f, mFieldRect.w};
    mFieldText = std::make_unique<TextComponent>("", mFieldFont, TEXT, ALIGN_LEFT, ALIGN_CENTER,
                                                 glm::ivec2 {0, 0}, textPos, textSize);
    mPlaceholderText =
        std::make_unique<TextComponent>("Search games", mFieldFont, DIM_TEXT, ALIGN_LEFT,
                                        ALIGN_CENTER, glm::ivec2 {0, 0}, textPos, textSize);

    auto scopeText = [this](const glm::vec4& rect, const std::string& label) {
        auto text = std::make_unique<TextComponent>(
            label, mSmallFont, TEXT, ALIGN_CENTER, ALIGN_CENTER, glm::ivec2 {0, 0},
            glm::vec3 {rect.x + rect.w * 0.2f, rect.y, 0.0f},
            glm::vec2 {rect.z - rect.w * 0.4f, rect.w});
        return text;
    };
    std::string thisLabel {"THIS CONSOLE"};
    if (mSystem != nullptr)
        thisLabel = Utils::String::toUpper(mSystem->getFullName());
    mScopeThisText = scopeText(mScopeThisRect, thisLabel);
    mScopeAllText = scopeText(mScopeAllRect, "ALL CONSOLES");

    mStatusText = std::make_unique<TextComponent>(
        "", mSmallFont, DIM_TEXT, ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0},
        glm::vec3 {mResultsRect.x, 0.175f * h, 0.0f}, glm::vec2 {mResultsRect.z, 0.045f * h});

    mEmptyText = std::make_unique<TextComponent>("", mNameFont, DIM_TEXT, ALIGN_CENTER,
                                                 ALIGN_CENTER, glm::ivec2 {0, 0},
                                                 glm::vec3 {mResultsRect.x, mResultsRect.y, 0.0f},
                                                 glm::vec2 {mResultsRect.z, mRowHeight * 2.0f});

    // On-screen keyboard: 10 columns, 4 character rows plus SPACE / DELETE / CLEAR.
    const float keyAreaX {0.05f * w};
    const float keyAreaW {0.37f * w};
    const float gap {0.006f * w};
    const float keyW {(keyAreaW - gap * 9.0f) / 10.0f};
    const float keyH {keyW * 1.1f};
    const float keyAreaY {mResultsRect.y};
    const std::shared_ptr<Font> keyFont {Font::get(0.032f * h)};

    auto addKey = [&](const std::string& label, KeyAction action, int row, int col, int span) {
        Key key;
        key.label = label;
        key.action = action;
        key.row = row;
        key.col = col;
        key.span = span;
        key.rect = {keyAreaX + col * (keyW + gap), keyAreaY + row * (keyH + gap),
                    keyW * span + gap * (span - 1), keyH};
        key.text = std::make_unique<TextComponent>(
            action == KeyAction::CHARACTER ? Utils::String::toUpper(label) : label,
            action == KeyAction::CHARACTER ? keyFont : mSmallFont, TEXT, ALIGN_CENTER, ALIGN_CENTER,
            glm::ivec2 {0, 0}, glm::vec3 {key.rect.x, key.rect.y, 0.0f},
            glm::vec2 {key.rect.z, key.rect.w});
        mKeys.emplace_back(std::move(key));
    };

    for (int row {0}; row < static_cast<int>(KEY_ROWS.size()); ++row) {
        for (int col {0}; col < static_cast<int>(KEY_ROWS[row].size()); ++col)
            addKey(std::string(1, KEY_ROWS[row][col]), KeyAction::CHARACTER, row, col, 1);
    }
    const int lastRow {static_cast<int>(KEY_ROWS.size())};
    addKey("SPACE", KeyAction::SPACE, lastRow, 0, 5);
    addKey("DELETE", KeyAction::BACKSPACE, lastRow, 5, 3);
    addKey("CLEAR", KeyAction::CLEAR, lastRow, 8, 2);

    // Result rows: cover, name and console.
    const float coverSize {mRowHeight * 0.84f};
    for (int i {0}; i < VISIBLE_ROWS; ++i) {
        Row row;
        const float y {mResultsRect.y + i * mRowHeight};
        row.cover = std::make_unique<ImageComponent>(false, true);
        row.cover->setOrigin(0.5f, 0.5f);
        row.cover->setPosition(mResultsRect.x + mRowHeight * 0.55f, y + mRowHeight * 0.5f);
        row.cover->setMaxSize(coverSize, coverSize);
        // One line high, so long names end in "..." instead of wrapping.
        const float textX {mResultsRect.x + mRowHeight * 1.2f};
        const float nameH {mNameFont->getHeight()};
        const float consoleH {mSmallFont->getHeight()};
        row.name = std::make_unique<TextComponent>(
            "", mNameFont, TEXT, ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0},
            glm::vec3 {textX, y + (mRowHeight - nameH) * 0.5f, 0.0f},
            glm::vec2 {mResultsRect.x + mResultsRect.z * 0.70f - textX, nameH});
        row.console = std::make_unique<TextComponent>(
            "", mSmallFont, DIM_TEXT, ALIGN_RIGHT, ALIGN_CENTER, glm::ivec2 {0, 0},
            glm::vec3 {mResultsRect.x + mResultsRect.z * 0.71f, y + (mRowHeight - consoleH) * 0.5f,
                       0.0f},
            glm::vec2 {mResultsRect.z * 0.27f, consoleH});
        mRows.emplace_back(std::move(row));
    }
}

void GuiGameSearch::runSearch()
{
    const std::string normQuery {normalize(mQuery)};
    std::vector<std::string> words;
    for (const std::string& word : Utils::String::delimitedStringToVector(normQuery, " ")) {
        if (!word.empty())
            words.emplace_back(word);
    }

    mResults.clear();
    const std::vector<Entry>& source {mScopeAll ? mAllEntries : mSystemEntries};
    for (const Entry& entry : source) {
        const int score {matchScore(entry.normName, entry.initials, words, normQuery)};
        if (score > 0)
            mResults.push_back({&entry, score});
    }
    // Entries are already sorted by name, so equal scores keep alphabetical order.
    std::stable_sort(mResults.begin(), mResults.end(),
                     [](const Result& a, const Result& b) { return a.score > b.score; });

    mResultCursor = 0;
    mResultScroll = 0;
    if (mResults.empty() && mFocus == Focus::RESULTS)
        mFocus = Focus::KEYBOARD;
    updateRows();
    updateStatus();
    updateHelpPrompts();
}

void GuiGameSearch::updateStatus()
{
    mFieldText->setText(mQuery, true);

    std::string status {std::to_string(mResults.size()) +
                        (mResults.size() == 1 ? " game" : " games")};
    if (mScopeAll)
        status.append("  -  all consoles");
    else if (mSystem != nullptr)
        status.append("  -  ").append(mSystem->getFullName());
    mStatusText->setText(status);

    if (mResults.empty())
        mEmptyText->setText(mQuery.empty() ? "No games here" : "No games match \"" + mQuery + "\"");
}

void GuiGameSearch::updateRows()
{
    if (mResultCursor < mResultScroll)
        mResultScroll = mResultCursor;
    else if (mResultCursor >= mResultScroll + VISIBLE_ROWS)
        mResultScroll = mResultCursor - VISIBLE_ROWS + 1;

    for (int i {0}; i < VISIBLE_ROWS; ++i) {
        Row& row {mRows[i]};
        const int index {mResultScroll + i};
        const Entry* entry {index < static_cast<int>(mResults.size()) ? mResults[index].entry :
                                                                        nullptr};
        if (entry == row.shown)
            continue;
        row.shown = entry;
        if (entry == nullptr)
            continue;
        row.name->setText(entry->name);
        row.console->setText(mScopeAll || entry->game->getSystem() != mSystem ? entry->console :
                                                                                "");
        std::string image {entry->game->getCoverPath()};
        if (image.empty())
            image = entry->game->getImagePath();
        row.cover->setImage(image);
    }
}

void GuiGameSearch::setScopeAll(bool all)
{
    if (all == mScopeAll)
        return;
    if (!all && mSystem == nullptr)
        return;
    mScopeAll = all;
    runSearch();
}

void GuiGameSearch::pressKey(const Key& key)
{
    switch (key.action) {
        case KeyAction::CHARACTER:
            if (mQuery.size() < MAX_QUERY)
                mQuery.append(key.label);
            break;
        case KeyAction::SPACE:
            if (!mQuery.empty() && mQuery.back() != ' ' && mQuery.size() < MAX_QUERY)
                mQuery.push_back(' ');
            break;
        case KeyAction::BACKSPACE:
            backspace();
            return;
        case KeyAction::CLEAR:
            mQuery.clear();
            break;
    }
    runSearch();
}

void GuiGameSearch::backspace()
{
    if (mQuery.empty())
        return;
    mQuery.erase(Utils::String::prevCursor(mQuery, mQuery.size()));
    runSearch();
}

void GuiGameSearch::moveKeyCursor(int dx, int dy)
{
    const Key& current {mKeys[mKeyCursor]};
    const int rows {static_cast<int>(KEY_ROWS.size()) + 1};

    auto keyCovering = [this](int row, int col) {
        for (int i {0}; i < static_cast<int>(mKeys.size()); ++i) {
            if (mKeys[i].row == row && col >= mKeys[i].col && col < mKeys[i].col + mKeys[i].span)
                return i;
        }
        return -1;
    };

    if (dy != 0) {
        const int row {(current.row + dy + rows) % rows};
        const int target {keyCovering(row, current.col)};
        if (target != -1)
            mKeyCursor = target;
        return;
    }

    if (dx > 0) {
        const int target {keyCovering(current.row, current.col + current.span)};
        if (target != -1)
            mKeyCursor = target;
        else if (!mResults.empty())
            mFocus = Focus::RESULTS; // Off the right edge into the results.
        else
            mKeyCursor = keyCovering(current.row, 0);
    }
    else if (dx < 0) {
        int target {keyCovering(current.row, current.col - 1)};
        if (target == -1)
            target = keyCovering(current.row, 9);
        mKeyCursor = target;
    }
}

void GuiGameSearch::moveResultCursor(int amount)
{
    if (mResults.empty())
        return;
    mResultCursor = std::clamp(mResultCursor + amount, 0, static_cast<int>(mResults.size()) - 1);
    updateRows();
}

void GuiGameSearch::navigate(int dx, int dy)
{
    if (mFocus == Focus::KEYBOARD) {
        moveKeyCursor(dx, dy);
    }
    else if (dx < 0) {
        mFocus = Focus::KEYBOARD;
    }
    else if (dy != 0) {
        moveResultCursor(dy);
    }
    updateHelpPrompts();
}

void GuiGameSearch::launchSelected()
{
    if (mResults.empty())
        return;
    FileData* game {mResults[mResultCursor].entry->game};
    SystemData* listSystem {mScopeAll ? game->getSystem() : mSystem};
    close();

    // Same steps as launching from the screensaver: start the game, and leave the gamelist on
    // it so ES-DE returns there afterwards.
    ViewController* view {ViewController::getInstance()};
    view->triggerGameLaunch(game);
    view->goToGamelist(listSystem);
    GamelistView* gamelist {view->getGamelistView(listSystem).get()};
    gamelist->setCursor(game);
    gamelist->stopListScrolling();
    view->cancelViewTransitions();
    view->pauseViewVideos();
}

void GuiGameSearch::goToSelected()
{
    if (mResults.empty())
        return;
    FileData* game {mResults[mResultCursor].entry->game};
    SystemData* listSystem {mScopeAll ? game->getSystem() : mSystem};
    close();

    ViewController* view {ViewController::getInstance()};
    view->goToGamelist(listSystem);
    GamelistView* gamelist {view->getGamelistView(listSystem).get()};
    gamelist->setCursor(game);
    gamelist->stopListScrolling();
    view->cancelViewTransitions();
}

void GuiGameSearch::close()
{
    ViewController::getInstance()->startViewVideos();
    delete this;
}

bool GuiGameSearch::input(InputConfig* config, Input input)
{
    // Physical keyboard: letters arrive through textInput(), keys are handled by symbol so
    // the default keyboard mapping (Backspace = "b", Escape = "start") does not interfere.
    if (config->getDeviceId() == DEVICE_KEYBOARD) {
        const bool arrow {input.id == SDLK_UP || input.id == SDLK_DOWN};
        if (input.value == 0) {
            if (arrow)
                mRepeatDy = 0;
            return true;
        }
        const bool shift {(SDL_GetModState() & KMOD_SHIFT) != 0};
        switch (input.id) {
            case SDLK_ESCAPE:
            case SDLK_F1:
            case SDLK_F3:
                close();
                return true;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (shift)
                    goToSelected();
                else
                    launchSelected();
                return true;
            case SDLK_TAB:
                setScopeAll(!mScopeAll);
                return true;
            case SDLK_UP:
            case SDLK_DOWN:
                if (!mResults.empty()) {
                    mRepeatDx = 0;
                    mRepeatDy = input.id == SDLK_UP ? -1 : 1;
                    mRepeatTimer = -REPEAT_DELAY;
                    // The first press only moves from the typing to the top result.
                    if (mFocus == Focus::KEYBOARD) {
                        mFocus = Focus::RESULTS;
                        updateHelpPrompts();
                    }
                    else {
                        navigate(0, mRepeatDy);
                    }
                }
                return true;
            case SDLK_PAGEUP:
                moveResultCursor(-VISIBLE_ROWS);
                return true;
            case SDLK_PAGEDOWN:
                moveResultCursor(VISIBLE_ROWS);
                return true;
            default:
                return true;
        }
    }

    const bool up {config->isMappedLike("up", input)};
    const bool down {config->isMappedLike("down", input)};
    const bool left {config->isMappedLike("left", input)};
    const bool right {config->isMappedLike("right", input)};

    if (up || down || left || right) {
        if (input.value == 0) {
            mRepeatDx = 0;
            mRepeatDy = 0;
            return true;
        }
        mRepeatDx = left ? -1 : (right ? 1 : 0);
        mRepeatDy = up ? -1 : (down ? 1 : 0);
        mRepeatTimer = -REPEAT_DELAY;
        navigate(mRepeatDx, mRepeatDy);
        return true;
    }

    if (input.value == 0)
        return true;

    if (config->isMappedTo("a", input)) {
        if (mFocus == Focus::KEYBOARD)
            pressKey(mKeys[mKeyCursor]);
        else
            launchSelected();
    }
    else if (config->isMappedTo("b", input)) {
        if (mFocus == Focus::RESULTS) {
            mFocus = Focus::KEYBOARD;
            updateHelpPrompts();
        }
        else if (!mQuery.empty()) {
            backspace();
        }
        else {
            close();
        }
    }
    else if (config->isMappedTo("x", input)) {
        setScopeAll(!mScopeAll);
    }
    else if (config->isMappedTo("y", input)) {
        if (mFocus == Focus::RESULTS)
            goToSelected();
        else
            pressKey(mKeys[mKeys.size() - 3]); // SPACE
    }
    else if (config->isMappedTo("start", input)) {
        launchSelected();
    }
    else if (config->isMappedTo("back", input) ||
             config->isMappedTo("rightthumbstickclick", input)) {
        close();
    }
    else if (config->isMappedLike("leftshoulder", input) ||
             config->isMappedLike("lefttrigger", input)) {
        moveResultCursor(-VISIBLE_ROWS);
    }
    else if (config->isMappedLike("rightshoulder", input) ||
             config->isMappedLike("righttrigger", input)) {
        moveResultCursor(VISIBLE_ROWS);
    }
    return true;
}

void GuiGameSearch::textInput(const std::string& text, const bool pasting)
{
    if (text == "\b") {
        backspace();
        return;
    }
    bool changed {false};
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || mQuery.size() >= MAX_QUERY)
            continue;
        mQuery.push_back(c);
        changed = true;
    }
    if (changed)
        runSearch();
}

bool GuiGameSearch::inside(const glm::vec4& rect, const glm::vec2& pos)
{
    return pos.x >= rect.x && pos.y >= rect.y && pos.x <= rect.x + rect.z &&
           pos.y <= rect.y + rect.w;
}

int GuiGameSearch::keyAt(const glm::vec2& pos) const
{
    for (int i {0}; i < static_cast<int>(mKeys.size()); ++i) {
        if (inside(mKeys[i].rect, pos))
            return i;
    }
    return -1;
}

int GuiGameSearch::resultRowAt(const glm::vec2& pos) const
{
    if (!inside(mResultsRect, pos))
        return -1;
    const int index {mResultScroll + static_cast<int>((pos.y - mResultsRect.y) / mRowHeight)};
    return index < static_cast<int>(mResults.size()) ? index : -1;
}

bool GuiGameSearch::mouseInput(const MouseEvent& event)
{
    switch (event.type) {
        case MouseEvent::Type::MOTION:
            mHoverKey = keyAt(event.pos);
            mHoverRow = resultRowAt(event.pos);
            return true;
        case MouseEvent::Type::WHEEL: {
            // Several rows per notch, so long lists are quick to get through.
            const int rows {std::max(1, static_cast<int>(std::abs(event.wheel) * 3.0f))};
            if (!mResults.empty()) {
                const int maxScroll {std::max(0, static_cast<int>(mResults.size()) - VISIBLE_ROWS)};
                mResultScroll =
                    std::clamp(mResultScroll + (event.wheel > 0.0f ? -rows : rows), 0, maxScroll);
                mResultCursor =
                    std::clamp(mResultCursor, mResultScroll, mResultScroll + VISIBLE_ROWS - 1);
                mResultCursor = std::min(mResultCursor, static_cast<int>(mResults.size()) - 1);
                updateRows();
                mHoverRow = resultRowAt(event.pos);
            }
            return true;
        }
        case MouseEvent::Type::BUTTON_UP:
            return true;
        case MouseEvent::Type::BUTTON_DOWN:
            break;
    }

    if (event.button == SDL_BUTTON_RIGHT || event.button == SDL_BUTTON_X1) {
        close();
        return true;
    }
    if (event.button != SDL_BUTTON_LEFT)
        return true;

    if (inside(mScopeThisRect, event.pos)) {
        setScopeAll(false);
        return true;
    }
    if (inside(mScopeAllRect, event.pos)) {
        setScopeAll(true);
        return true;
    }

    const int key {keyAt(event.pos)};
    if (key != -1) {
        mFocus = Focus::KEYBOARD;
        mKeyCursor = key;
        pressKey(mKeys[key]);
        updateHelpPrompts();
        return true;
    }

    const int row {resultRowAt(event.pos)};
    if (row != -1) {
        // First click selects, a click on the selected game (or a double click) plays it.
        const bool selected {mFocus == Focus::RESULTS && row == mResultCursor};
        mFocus = Focus::RESULTS;
        mResultCursor = row;
        updateRows();
        updateHelpPrompts();
        if (selected || event.clicks >= 2)
            launchSelected();
    }
    return true;
}

void GuiGameSearch::update(int deltaTime)
{
    mCaretTimer = (mCaretTimer + deltaTime) % 1000;

    if (mRepeatDx != 0 || mRepeatDy != 0) {
        mRepeatTimer += deltaTime;
        while (mRepeatTimer >= REPEAT_INTERVAL) {
            mRepeatTimer -= REPEAT_INTERVAL;
            navigate(mRepeatDx, mRepeatDy);
        }
    }

    for (Row& row : mRows)
        row.cover->update(deltaTime);
    GuiComponent::update(deltaTime);
}

void GuiGameSearch::render(const glm::mat4& parentTrans)
{
    const glm::mat4 trans {parentTrans * getTransform()};
    mRenderer->setMatrix(trans);

    // Rounded corners are computed from the rectangle's local origin, so translate to it.
    auto rect = [this, &trans](const glm::vec4& r, unsigned int color, float radius) {
        mRenderer->setMatrix(glm::translate(trans, glm::round(glm::vec3 {r.x, r.y, 0.0f})));
        mRenderer->drawRect(0.0f, 0.0f, r.z, r.w, color, color, false, 1.0f, 1.0f,
                            Renderer::BlendFactor::SRC_ALPHA,
                            Renderer::BlendFactor::ONE_MINUS_SRC_ALPHA, radius);
    };

    rect({0.0f, 0.0f, mSize.x, mSize.y}, 0x0A0C12F2, 0.0f);

    // Search field with magnifier and caret.
    rect(mFieldRect, 0xFFFFFF24, mFieldRect.w * 0.5f);
    mMagnifier->render(trans);
    if (mQuery.empty())
        mPlaceholderText->render(trans);
    else
        mFieldText->render(trans);
    if (mCaretTimer < 600) {
        const float textW {std::min(mFieldFont->sizeText(mQuery).x, mFieldText->getSize().x)};
        const float caretX {mFieldText->getPosition().x + (mQuery.empty() ? 0.0f : textW + 2.0f)};
        rect({caretX, mFieldRect.y + mFieldRect.w * 0.22f, 3.0f, mFieldRect.w * 0.56f}, ACCENT,
             0.0f);
    }

    // Scope toggle.
    const glm::vec4 scopeAll {mScopeThisRect.x, mScopeThisRect.y,
                              mScopeThisRect.z + mScopeAllRect.z, mScopeThisRect.w};
    rect(scopeAll, 0xFFFFFF18, scopeAll.w * 0.5f);
    rect(mScopeAll ? mScopeAllRect : mScopeThisRect, ACCENT, mScopeThisRect.w * 0.5f);
    mScopeThisText->setColor(mScopeAll ? TEXT : DARK_TEXT);
    mScopeAllText->setColor(mScopeAll ? DARK_TEXT : TEXT);
    if (mSystem == nullptr)
        mScopeThisText->setColor(0xFFFFFF44);
    mScopeThisText->render(trans);
    mScopeAllText->render(trans);

    // Keyboard.
    const float keyRadius {mKeys.front().rect.w * 0.18f};
    for (int i {0}; i < static_cast<int>(mKeys.size()); ++i) {
        Key& key {mKeys[i]};
        const bool selected {mFocus == Focus::KEYBOARD && i == mKeyCursor};
        unsigned int color {0xFFFFFF14};
        if (selected)
            color = ACCENT;
        else if (i == mHoverKey)
            color = 0xFFFFFF33;
        else if (mFocus != Focus::KEYBOARD && i == mKeyCursor)
            color = 0xFFFFFF26;
        rect(key.rect, color, keyRadius);
        key.text->setColor(selected ? DARK_TEXT : TEXT);
        key.text->render(trans);
    }

    // Results.
    mStatusText->render(trans);
    if (mResults.empty()) {
        mEmptyText->render(trans);
    }
    else {
        for (int i {0}; i < VISIBLE_ROWS; ++i) {
            const int index {mResultScroll + i};
            if (index >= static_cast<int>(mResults.size()))
                break;
            const glm::vec4 rowRect {mResultsRect.x, mResultsRect.y + i * mRowHeight,
                                     mResultsRect.z, mRowHeight * 0.94f};
            if (index == mResultCursor) {
                rect(rowRect, mFocus == Focus::RESULTS ? 0xFFFFFF2E : 0xFFFFFF12,
                     mRowHeight * 0.12f);
                if (mFocus == Focus::RESULTS)
                    rect({rowRect.x, rowRect.y + rowRect.w * 0.15f, 5.0f, rowRect.w * 0.7f}, ACCENT,
                         2.0f);
            }
            else if (index == mHoverRow) {
                rect(rowRect, 0xFFFFFF14, mRowHeight * 0.12f);
            }
            mRows[i].cover->render(trans);
            mRows[i].name->render(trans);
            mRows[i].console->render(trans);
        }

        // Scrollbar.
        if (static_cast<int>(mResults.size()) > VISIBLE_ROWS) {
            const float trackX {mResultsRect.x + mResultsRect.z + 0.006f * mSize.x};
            const float barH {
                std::max(mResultsRect.w * VISIBLE_ROWS / mResults.size(), mRowHeight * 0.5f)};
            const float maxScroll {static_cast<float>(mResults.size() - VISIBLE_ROWS)};
            const float barY {mResultsRect.y + (mResultsRect.w - barH) * mResultScroll / maxScroll};
            rect({trackX, mResultsRect.y, 4.0f, mResultsRect.w}, 0xFFFFFF14, 2.0f);
            rect({trackX, barY, 4.0f, barH}, 0xFFFFFF88, 2.0f);
        }
    }
}

std::vector<HelpPrompt> GuiGameSearch::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts;
    prompts.push_back(HelpPrompt("up/down/left/right", "move"));
    if (mFocus == Focus::KEYBOARD) {
        prompts.push_back(HelpPrompt("a", "type"));
        prompts.push_back(HelpPrompt("b", mQuery.empty() ? "close" : "delete"));
        prompts.push_back(HelpPrompt("y", "space"));
    }
    else {
        prompts.push_back(HelpPrompt("a", "play"));
        prompts.push_back(HelpPrompt("b", "keyboard"));
        prompts.push_back(HelpPrompt("y", "show in list"));
    }
    prompts.push_back(HelpPrompt("x", mScopeAll ? "this console" : "all consoles"));
    if (!mResults.empty() && mFocus == Focus::KEYBOARD)
        prompts.push_back(HelpPrompt("start", "play"));
    return prompts;
}
