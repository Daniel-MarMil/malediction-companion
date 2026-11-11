// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "mc/core/deck_text.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

namespace mc::core {
namespace {

/// A real deck has a few dozen lines; the same limit as the JSON deck.
constexpr std::size_t max_section_lines = 500;
/// Longest piece of a line quoted in an error.
constexpr std::size_t max_quoted_bytes = 120;

constexpr std::string_view main_title = "Main Deck";
constexpr std::string_view side_title = "Side Deck";

bool is_blank(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_blank(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_blank(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// The key two names are compared by: ASCII letters in lowercase, ’ and ‘ as ', runs of spaces,
/// tabs and no-break spaces as one space, and no spaces at the ends.
std::string name_key(std::string_view name) {
    std::string key;
    key.reserve(name.size());
    bool pending_space = false;
    std::size_t i = 0;
    while (i < name.size()) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (is_blank(name[i])) {
            pending_space = !key.empty();
            ++i;
            continue;
        }
        if (c == 0xC2 && i + 1 < name.size() && static_cast<unsigned char>(name[i + 1]) == 0xA0) {
            pending_space = !key.empty();  // U+00A0, no-break space.
            i += 2;
            continue;
        }
        if (pending_space) {
            key += ' ';
            pending_space = false;
        }
        if (c == 0xE2 && i + 2 < name.size() && static_cast<unsigned char>(name[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(name[i + 2]) == 0x98 || static_cast<unsigned char>(name[i + 2]) == 0x99)) {
            key += '\'';  // U+2018 and U+2019, typographic apostrophes.
            i += 3;
            continue;
        }
        key += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : name[i];
        ++i;
    }
    return key;
}

/// Card names of the pack by their key; nullptr marks a name that several cards share.
class NameIndex {
public:
    explicit NameIndex(const RulesPack& pack) {
        cards_.reserve(pack.card_count());
        for (const Card& card : pack.cards()) {
            const auto [slot, inserted] = cards_.try_emplace(name_key(card.name), &card);
            if (!inserted) {
                slot->second = nullptr;
            }
        }
    }

    /// The card, nullptr when it is ambiguous, or nothing when there is none.
    [[nodiscard]] std::optional<const Card*> find(std::string_view name) const {
        const auto found = cards_.find(name_key(name));
        if (found == cards_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

private:
    std::unordered_map<std::string, const Card*> cards_;
};

/// "Nx Name": copies is 0 when the number is out of range.
struct CardLine {
    int copies;
    std::string_view name;
};

std::optional<CardLine> parse_card_line(std::string_view line) {
    std::size_t i = 0;
    int copies = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        copies = std::min((copies * 10) + (line[i] - '0'), max_deck_text_copies + 1);
        ++i;
    }
    if (i == 0) {
        return std::nullopt;
    }
    while (i < line.size() && is_blank(line[i])) {
        ++i;
    }
    if (i == line.size() || (line[i] != 'x' && line[i] != 'X')) {
        return std::nullopt;
    }
    ++i;
    if (i == line.size() || !is_blank(line[i])) {
        return std::nullopt;
    }
    const std::string_view name = trim(line.substr(i));
    if (name.empty()) {
        return std::nullopt;
    }
    return CardLine{.copies = copies <= max_deck_text_copies ? copies : 0, .name = name};
}

enum class Section : std::uint8_t { First, Main, Side, Unknown };

/// A section title, in any case, with or without a final colon.
std::optional<Section> parse_title(std::string_view line) {
    if (!line.empty() && line.back() == ':') {
        line = trim(line.substr(0, line.size() - 1));
    }
    const std::string key = name_key(line);
    if (key == name_key(main_title)) {
        return Section::Main;
    }
    if (key == name_key(side_title)) {
        return Section::Side;
    }
    return std::nullopt;
}

std::string quoted(std::string_view text) {
    if (text.size() <= max_quoted_bytes) {
        return std::string(text);
    }
    std::size_t end = max_quoted_bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;  // Not in the middle of a UTF-8 character.
    }
    return std::string(text.substr(0, end)) + "...";
}

bool valid_copies(int copies) noexcept {
    return copies >= 1 && copies <= max_deck_text_copies;
}

class Importer {
public:
    explicit Importer(const RulesPack& pack) : index_(pack) {}

    ImportedDeck run(std::string_view text) {
        constexpr std::string_view bom = "\xEF\xBB\xBF";
        if (text.starts_with(bom)) {
            text.remove_prefix(bom.size());
        }
        std::size_t number = 0;
        while (!text.empty()) {
            const std::size_t end = text.find('\n');
            const std::string_view line = text.substr(0, end);
            text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            ++number;
            read_line(number, trim(line));
        }
        return std::move(result_);
    }

private:
    void error(std::size_t line, DeckTextIssue issue, std::string detail) {
        if (result_.errors.size() == max_deck_text_errors) {
            result_.errors_truncated = true;
            return;
        }
        result_.errors.push_back({.line = line, .issue = issue, .detail = std::move(detail)});
    }

    /// Once Main Deck and Side Deck are ruled out: a line that does not start with a digit and
    /// is not the name of a card, such as Maybeboard. A card name alone is a card line that lacks
    /// its copies, so it does not hide the lines after it.
    [[nodiscard]] bool is_unknown_title(std::string_view line) const {
        const bool starts_with_digit = line.front() >= '0' && line.front() <= '9';
        return !starts_with_digit && !index_.find(line).has_value();
    }

    void read_line(std::size_t number, std::string_view line) {
        if (line.empty()) {
            return;
        }
        if (const auto title = parse_title(line)) {
            section_ = *title;
            return;
        }
        // Under an unknown title nothing is imported, so that no card lands in the wrong section.
        if (is_unknown_title(line)) {
            section_ = Section::Unknown;
            unknown_title_line_ = number;
            error(number, DeckTextIssue::UnknownSection,
                  "unknown section; nothing is imported until Main Deck or Side Deck: " + quoted(line));
            return;
        }
        if (section_ == Section::Unknown) {
            error(number, DeckTextIssue::UnknownSection,
                  "not imported, under the unknown section of line " + std::to_string(unknown_title_line_) + ": " +
                      quoted(line));
            return;
        }
        const auto card_line = parse_card_line(line);
        if (!card_line) {
            error(number, DeckTextIssue::UnrecognizedLine, "expected \"Nx Name\": " + quoted(line));
            return;
        }
        if (card_line->copies == 0) {
            error(number, DeckTextIssue::BadCopies, "copies must be from 1 to 99: " + quoted(line));
            return;
        }
        const auto card = index_.find(card_line->name);
        if (!card) {
            error(number, DeckTextIssue::UnknownCard, "no card of the pack is called " + quoted(card_line->name));
            return;
        }
        if (*card == nullptr) {
            error(number, DeckTextIssue::AmbiguousCard,
                  "several cards of the pack are called " + quoted(card_line->name));
            return;
        }
        if (section_ == Section::First) {
            add_first(number, **card, card_line->copies);
        } else {
            add_entry(number, section_ == Section::Main ? result_.deck.main : result_.deck.side, **card,
                      card_line->copies);
        }
    }

    /// Seeker, Legacy and terrain, in any order, before the first section title.
    void add_first(std::size_t number, const Card& card, int copies) {
        if (copies != 1) {
            error(number, DeckTextIssue::BadCopies, "the Seeker, the Legacy and the terrain go with 1x: " + card.name);
            return;
        }
        std::string* slot = nullptr;
        switch (card.type) {
            case CardType::Seeker:
                slot = &result_.deck.seeker_id;
                break;
            case CardType::Legacy:
                slot = &result_.deck.legacy_id;
                break;
            case CardType::Terrain:
                slot = &result_.deck.terrain_id;
                break;
            default:
                error(number, DeckTextIssue::WrongSection,
                      "before Main Deck only the Seeker, the Legacy and the terrain may appear: " + card.name);
                return;
        }
        if (!slot->empty()) {
            error(number, DeckTextIssue::WrongSection,
                  "there is already a " + std::string(core::to_string(card.type)) + ": " + card.name);
            return;
        }
        *slot = card.id;
    }

    void add_entry(std::size_t number, std::vector<DeckEntry>& entries, const Card& card, int copies) {
        const auto existing =
            std::ranges::find_if(entries, [&](const DeckEntry& entry) { return entry.card_id == card.id; });
        if (existing != entries.end()) {
            if (existing->copies + copies > max_deck_text_copies) {
                error(number, DeckTextIssue::BadCopies, "with its earlier lines it goes over 99 copies: " + card.name);
                return;
            }
            existing->copies += copies;
            return;
        }
        if (entries.size() == max_section_lines) {
            error(number, DeckTextIssue::TooManyLines, "a section holds at most 500 different cards");
            return;
        }
        entries.push_back({.card_id = card.id, .copies = copies});
    }

    NameIndex index_;
    Section section_ = Section::First;
    std::size_t unknown_title_line_ = 0;
    ImportedDeck result_;
};

/// The same lines with each card once, in the place of its first line.
std::vector<DeckEntry> merged(const std::vector<DeckEntry>& entries) {
    std::vector<DeckEntry> result;
    for (const DeckEntry& entry : entries) {
        const auto existing =
            std::ranges::find_if(result, [&](const DeckEntry& other) { return other.card_id == entry.card_id; });
        if (existing == result.end()) {
            result.push_back(entry);
        } else if (valid_copies(existing->copies) && valid_copies(entry.copies)) {
            existing->copies += entry.copies;  // At most 198; over 99 is reported as bad_copies.
        } else {
            existing->copies = 0;  // Reported as bad_copies; adding them could overflow.
        }
    }
    return result;
}

const Card& known_card(const RulesPack& pack, const std::string& id) {
    const Card* card = pack.find(id);
    if (card == nullptr) {
        throw DeckExportError("unknown_card", "unknown card: " + quoted(id));
    }
    return *card;
}

void write_first(std::string& text, const RulesPack& pack, const std::string& id, CardType type) {
    if (id.empty()) {
        return;
    }
    const Card& card = known_card(pack, id);
    if (card.type != type) {
        throw DeckExportError("wrong_section", card.id + " is not a " + std::string(core::to_string(type)));
    }
    text += "1x " + card.name + "\n";
}

void write_section(std::string& text, const RulesPack& pack, std::string_view title,
                   const std::vector<DeckEntry>& entries) {
    text += "\n";
    text += title;
    text += "\n";
    for (const DeckEntry& entry : entries) {
        const Card& card = known_card(pack, entry.card_id);
        if (!valid_copies(entry.copies)) {
            throw DeckExportError("bad_copies", card.id + ": copies must be from 1 to 99");
        }
        text += std::to_string(entry.copies) + "x " + card.name + "\n";
    }
}

}  // namespace

std::string_view to_string(DeckTextIssue issue) noexcept {
    switch (issue) {
        case DeckTextIssue::UnrecognizedLine:
            return "unrecognized_line";
        case DeckTextIssue::UnknownSection:
            return "unknown_section";
        case DeckTextIssue::BadCopies:
            return "bad_copies";
        case DeckTextIssue::UnknownCard:
            return "unknown_card";
        case DeckTextIssue::AmbiguousCard:
            return "ambiguous_card";
        case DeckTextIssue::WrongSection:
            return "wrong_section";
        case DeckTextIssue::TooManyLines:
            return "too_many_lines";
    }
    return "unrecognized_line";
}

ImportedDeck import_deck_text(const RulesPack& pack, std::string_view text) {
    return Importer(pack).run(text);
}

std::string export_deck_text(const RulesPack& pack, const Deck& deck) {
    Deck canonical = deck;
    canonical.main = merged(deck.main);
    canonical.side = merged(deck.side);

    std::string text;
    write_first(text, pack, canonical.seeker_id, CardType::Seeker);
    write_first(text, pack, canonical.legacy_id, CardType::Legacy);
    write_first(text, pack, canonical.terrain_id, CardType::Terrain);
    write_section(text, pack, main_title, canonical.main);
    if (!canonical.side.empty()) {
        write_section(text, pack, side_title, canonical.side);
    }
    if (text.front() == '\n') {
        text.erase(0, 1);  // A deck without Seeker, Legacy or terrain starts with Main Deck.
    }
    text.pop_back();  // No newline after the last line.

    // The list must read back as the same deck: names that several cards share, or that
    // would not survive the trip, are refused here instead of losing cards on import.
    const ImportedDeck back = import_deck_text(pack, text);
    if (!back.errors.empty() || back.deck != canonical) {
        throw DeckExportError("not_exportable", back.errors.empty()
                                                    ? std::string("the list does not read back as the same deck")
                                                    : back.errors.front().detail);
    }
    return text;
}

}  // namespace mc::core
