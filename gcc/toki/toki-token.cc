#include "toki-token.h"

namespace Toki
{

const char *
get_token_description (TokenId tid)
{
  switch (tid)
    {
#define TOKI_TOKEN(name, descr)                                                \
  case name:                                                                   \
    return descr;
#define TOKI_TOKEN_KEYWORD(x, y) TOKI_TOKEN (x, y)
      TOKI_TOKEN_LIST
#undef TOKI_TOKEN_KEYWORD
#undef TOKI_TOKEN
    default:
      gcc_unreachable ();
    }
}

const char *
token_id_to_str (TokenId tid)
{
  switch (tid)
    {
#define TOKI_TOKEN(name, _)                                                    \
  case name:                                                                   \
    return #name;
#define TOKI_TOKEN_KEYWORD(x, y) TOKI_TOKEN (x, y)
      TOKI_TOKEN_LIST
#undef TOKI_TOKEN_KEYWORD
#undef TOKI_TOKEN
    default:
      gcc_unreachable ();
    }
}

}
