#ifndef TOKI_TOKEN_H
#define TOKI_TOKEN_H

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "input.h"

#include <string>
#include <tr1/memory>

namespace Toki
{

// TOKI_TOKEN(name, description)
// TOKI_TOKEN_KEYWORD(name, identifier)
//
// Keep TOKI_TOKEN_KEYWORD sorted

#define TOKI_TOKEN_LIST                                                        \
  TOKI_TOKEN (FIRST_TOKEN, "<first-token-marker>")                             \
  TOKI_TOKEN (END_OF_FILE, "end of file")                                      \
  TOKI_TOKEN (ASTERISK, "*")                                                   \
  TOKI_TOKEN (COLON, ":")                                                      \
  TOKI_TOKEN (COMMA, ",")                                                      \
  TOKI_TOKEN (DIFFERENT, "!=")                                                 \
  TOKI_TOKEN (EQUAL, "=")                                                      \
  TOKI_TOKEN (LEFT_PAREN, "(")                                                 \
  TOKI_TOKEN (MINUS, "-")                                                      \
  TOKI_TOKEN (PLUS, "+")                                                       \
  TOKI_TOKEN (RIGHT_PAREN, ")")                                                \
  TOKI_TOKEN (SEMICOLON, ";")                                                  \
  TOKI_TOKEN (EOL, "\n")                                                       \
  TOKI_TOKEN (SLASH, "/")                                                      \
  TOKI_TOKEN (PERCENT, "%")                                                    \
  TOKI_TOKEN (GREATER, ">")                                                    \
  TOKI_TOKEN (GREATER_OR_EQUAL, ">=")                                          \
  TOKI_TOKEN (LOWER, "<")                                                      \
  TOKI_TOKEN (LOWER_OR_EQUAL, "<=")                                            \
  TOKI_TOKEN (IDENTIFIER, "identifier")                                        \
  TOKI_TOKEN (INTEGER_LITERAL, "integer literal")                              \
  TOKI_TOKEN (REAL_LITERAL, "real literal")                                    \
  TOKI_TOKEN (STRING_LITERAL, "string literal")                                \
  TOKI_TOKEN (LEFT_SQUARE, "[")                                                \
  TOKI_TOKEN (RIGHT_SQUARE, "]")                                               \
  TOKI_TOKEN (LEFT_BRACE, "{")                                                 \
  TOKI_TOKEN (RIGHT_BRACE, "}")                                                \
  TOKI_TOKEN (DOT, ".")                                                        \
                                                                               \
  TOKI_TOKEN_KEYWORD (ALA, "ala")                                              \
  TOKI_TOKEN_KEYWORD (ANTE, "ante")                                            \
  TOKI_TOKEN_KEYWORD (ANU, "anu")                                              \
  TOKI_TOKEN_KEYWORD (E, "e")                                                  \
  TOKI_TOKEN_KEYWORD (EN, "en")                                                \
  TOKI_TOKEN_KEYWORD (IJO, "ijo")                                              \
  TOKI_TOKEN_KEYWORD (JAKI, "jaki")                                            \
  TOKI_TOKEN_KEYWORD (JO, "jo")                                                \
  TOKI_TOKEN_KEYWORD (KAMA, "kama")                                            \
  TOKI_TOKEN_KEYWORD (LA, "la")                                                \
  TOKI_TOKEN_KEYWORD (LI, "li")                                                \
  TOKI_TOKEN_KEYWORD (LON, "lon")                                              \
  TOKI_TOKEN_KEYWORD (LUKIN, "lukin")                                          \
  TOKI_TOKEN_KEYWORD (MUTE, "mute")                                            \
  TOKI_TOKEN_KEYWORD (NANPA, "nanpa")                                          \
  TOKI_TOKEN_KEYWORD (NASIN, "nasin")                                          \
  TOKI_TOKEN_KEYWORD (NI, "ni")                                                \
  TOKI_TOKEN_KEYWORD (NIMI, "nimi")                                            \
  TOKI_TOKEN_KEYWORD (O, "o")                                                  \
  TOKI_TOKEN_KEYWORD (OPEN, "open")                                            \
  TOKI_TOKEN_KEYWORD (PANA, "pana")                                            \
  TOKI_TOKEN_KEYWORD (PINI, "pini")                                            \
  TOKI_TOKEN_KEYWORD (SIN, "sin")                                              \
  TOKI_TOKEN_KEYWORD (TOKI, "toki")                                            \
                                                                               \
  TOKI_TOKEN (LAST_TOKEN, "<last-token-marker>")

enum TokenId
{
#define TOKI_TOKEN(name, _) name,
#define TOKI_TOKEN_KEYWORD(x, y) TOKI_TOKEN (x, y)
  TOKI_TOKEN_LIST
#undef TOKI_TOKEN_KEYWORD
#undef TOKI_TOKEN
};

const char *get_token_description (TokenId tid);
const char *token_id_to_str (TokenId tid);

struct Token;
typedef std::tr1::shared_ptr<Token> TokenPtr;
typedef std::tr1::shared_ptr<const Token> const_TokenPtr;

struct Token
{
private:
  TokenId token_id;
  location_t locus;
  std::string *str;

  Token (TokenId token_id_, location_t locus_)
    : token_id (token_id_), locus (locus_), str (0)
  {
  }
  Token (TokenId token_id_, location_t locus_, const std::string& str_)
    : token_id (token_id_), locus (locus_), str (new std::string (str_))
  {
  }

  // No default initializer
  Token ();
  // Do not copy/assign tokens
  Token (const Token &);
  Token &operator=(const Token &);

public:
  ~Token () { delete str; }

  static TokenPtr
  make (TokenId token_id, location_t locus)
  {
    return TokenPtr(new Token (token_id, locus));
  }

  static TokenPtr
  make_identifier (location_t locus, const std::string& str)
  {
    return TokenPtr(new Token (IDENTIFIER, locus, str));
  }

  static TokenPtr
  make_integer (location_t locus, const std::string& str)
  {
    return TokenPtr(new Token (INTEGER_LITERAL, locus, str));
  }

  static TokenPtr
  make_real (location_t locus, const std::string& str)
  {
    return TokenPtr(new Token (REAL_LITERAL, locus, str));
  }

  static TokenPtr
  make_string (location_t locus, const std::string& str)
  {
    return TokenPtr(new Token (STRING_LITERAL, locus, str));
  }

  TokenId
  get_id () const
  {
    return token_id;
  }

  location_t
  get_locus () const
  {
    return locus;
  }

  const std::string &
  get_str () const
  {
    gcc_assert (str != NULL);
    return *str;
  }

  // diagnostics
  const char *
  get_token_description () const
  {
    return Toki::get_token_description (token_id);
  }

  // debugging
  const char *
  token_id_to_str () const
  {
    return Toki::token_id_to_str (token_id);
  }
};

}

#endif // TOKI_TOKEN_H
