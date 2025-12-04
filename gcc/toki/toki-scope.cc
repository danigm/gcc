#include "toki-scope.h"

namespace Toki
{

Scope::Scope ()
{
}

void
Scope::push_scope ()
{
  map_stack.push_back (SymbolMapping());
  fn_stack.push_back (FnMapping ());
}

void
Scope::pop_scope ()
{
  gcc_assert (!map_stack.empty());
  map_stack.pop_back ();

  gcc_assert (!fn_stack.empty());
  fn_stack.pop_back ();
}

SymbolPtr
Scope::lookup (const std::string &str)
{
  for (MapStack::reverse_iterator map = map_stack.rbegin ();
       map != map_stack.rend (); map++)
    {
      if (SymbolPtr sym = map->get (str))
	{
	  return sym;
	}
    }
  return SymbolPtr();
}

Tree
Scope::lookup_fn (const std::string &str)
{
  for (FnStack::reverse_iterator map = fn_stack.rbegin ();
       map != fn_stack.rend (); map++)
    {
      FnMapping::const_iterator it = map->find (str);
      if (it != map->end ())
	return it->second;
    }
  return NULL_TREE;
}
}
