#ifndef TOKI_SCOPE_H
#define TOKI_SCOPE_H

#include "toki-symbol-mapping.h"
#include "toki-tree.h"
#include <tr1/memory>
#include <vector>
#include <map>

namespace Toki
{

typedef std::map<std::string, Tree> FnMapping;
typedef std::vector<FnMapping> FnStack;
typedef std::vector<SymbolMapping> MapStack;

struct Scope
{
public:
  SymbolMapping &
  get_current_mapping ()
  {
    gcc_assert (!map_stack.empty ());
    return map_stack.back ();
  }

  FnMapping &
  get_current_fn ()
  {
    gcc_assert (!fn_stack.empty ());
    return fn_stack.back ();
  }

  void push_scope ();
  void pop_scope ();

  Scope ();

  SymbolPtr lookup (const std::string &str);
  Tree lookup_fn (const std::string &str);

private:
  MapStack map_stack;
  FnStack fn_stack;
};

}

#endif // TOKI_SCOPE_H
