#ifndef TOKI_SYMBOL_MAPPING_H
#define TOKI_SYMBOL_MAPPING_H

#include "toki/toki-symbol.h"
#include <tr1/memory>
#include <map>

namespace Toki
{

struct SymbolMapping
{
public:

  void insert (SymbolPtr s);
  SymbolPtr get (const std::string &str) const;

private:

  typedef std::map<std::string, SymbolPtr > Map;
  Map map;
};

}

#endif // TOKI_SYMBOL_MAPPING_H
