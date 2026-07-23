#include "board.h"

#include <cctype>

namespace Mockingbird {

std::string square_name(Square square) {
    // A single sentinel spelling is easier for diagnostic output than trying
    // to assign coordinates to padding and corner cut-outs.
    if (!is_ok(square))
        return "-";

    // Files are stored as 1..14, so FILE_A must be removed before adding the
    // zero-based offset to the character 'a'.
    std::string name;
    name += char('a' + file_of(square) - FILE_A);
    name += std::to_string(rank_of(square));
    return name;
}

std::optional<Square> parse_square(std::string_view name) {
    // Legal names range from two characters ("d1") to three ("k14").
    if (name.size() < 2 || name.size() > 3)
        return std::nullopt;

    // std::tolower requires an unsigned-char value (or EOF). Converting before
    // the call avoids undefined behavior for negative plain-char values.
    const unsigned char first = static_cast<unsigned char>(name.front());
    const char file_character = char(std::tolower(first));
    if (file_character < 'a' || file_character > 'n')
        return std::nullopt;

    // Ranks contain at most two digits, so this small allocation-free parser is
    // straightforward and rejects signs, whitespace, and trailing characters.
    int rank = 0;
    for (const char character : name.substr(1)) {
        if (character < '0' || character > '9')
            return std::nullopt;
        rank = rank * 10 + (character - '0');
    }

    if (rank < RANK_1 || rank > RANK_14)
        return std::nullopt;

    const File file = File(file_character - 'a' + FILE_A);
    const Square square = make_square(file, Rank(rank));

    // A syntactically valid coordinate such as "a1" may still refer to one of
    // the four cut-out corners. Only playable squares are accepted.
    return is_ok(square) ? std::optional(square) : std::nullopt;
}

}  // namespace Mockingbird
