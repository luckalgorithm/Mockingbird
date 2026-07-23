#include "board.h"

#include <cctype>

namespace Mockingbird {

std::string square_name(Square square) {
    // Invalid and non-playable squares use the sentinel spelling "-".
    if (!is_ok(square))
        return "-";

    // File values 1..14 map to characters 'a'..'n'.
    std::string name;
    name += char('a' + file_of(square) - FILE_A);
    name += std::to_string(rank_of(square));
    return name;
}

std::optional<Square> parse_square(std::string_view name) {
    // Coordinate names contain one file character and a one- or two-digit rank.
    if (name.size() < 2 || name.size() > 3)
        return std::nullopt;

    // std::tolower requires an unsigned-char value (or EOF). Converting before
    // the call avoids undefined behavior for negative plain-char values.
    const unsigned char first = static_cast<unsigned char>(name.front());
    const char file_character = char(std::tolower(first));
    if (file_character < 'a' || file_character > 'n')
        return std::nullopt;

    // The loop accepts decimal digits only.
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

    // Coordinates in the four cut-out corners are not playable.
    return is_ok(square) ? std::optional(square) : std::nullopt;
}

}  // namespace Mockingbird
