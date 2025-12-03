/* Toki parser */

#include <iostream>
#include <memory>

#include "toki/toki-parser.h"
#include "toki/toki-lexer.h"
#include "toki/toki-tree.h"
#include "toki/toki-symbol.h"
#include "toki/toki-symbol-mapping.h"
#include "toki/toki-scope.h"

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "tree.h"
#include "tree-iterator.h"
#include "input.h"
#include "diagnostic.h"
#include "stringpool.h"
#include "cgraph.h"
#include "gimplify.h"
#include "gimple-expr.h"
#include "convert.h"
#include "print-tree.h"
#include "stor-layout.h"
#include "fold-const.h"

namespace Toki
{

struct Parser
{
private:
  void skip_after_eol ();

  bool skip_token (TokenId);
  void skip_eol ();
  const_TokenPtr expect_token (TokenId);
  const_TokenPtr expect_token_skip_eol (Toki::TokenId token_id);
  void unexpected_token (const_TokenPtr);

  // Expression parsing
  int left_binding_power (const_TokenPtr tok);
  Tree null_denotation (const_TokenPtr tok);
  Tree left_denotation (const_TokenPtr tok, Tree left);

  Tree parse_expression (int right_binding_power);

  Tree coerce_binary_arithmetic (const_TokenPtr tok, Tree *left, Tree *right);
  bool check_logical_operands (const_TokenPtr tok, Tree left, Tree right);

  Tree get_printf_addr ();
  Tree get_puts_addr ();

  const char *print_type (Tree type);

  TreeStmtList &get_current_stmt_list ();

  void enter_scope ();

  struct TreeSymbolMapping
  {
    Tree bind_expr;
    Tree block;
  };

  TreeSymbolMapping leave_scope ();

  SymbolPtr query_type (const std::string &name, location_t loc);
  SymbolPtr query_variable (const std::string &name, location_t loc);
  SymbolPtr query_integer_variable (const std::string &name, location_t loc);

  void parse_statement_seq (bool (Parser::*done) ());

  bool done_end_of_file ();
  bool done_right_brace ();

  typedef Tree (Parser::*BinaryHandler) (const_TokenPtr, Tree);
  BinaryHandler get_binary_handler (TokenId id);

#define BINARY_HANDLER_LIST                                                    \
  BINARY_HANDLER (plus, PLUS)                                                  \
  BINARY_HANDLER (minus, MINUS)                                                \
  BINARY_HANDLER (mult, ASTERISK)                                              \
  BINARY_HANDLER (div, SLASH)                                                  \
  BINARY_HANDLER (mod, PERCENT)                                                \
                                                                               \
  BINARY_HANDLER (equal, EQUAL)                                                \
  BINARY_HANDLER (different, DIFFERENT)                                        \
  BINARY_HANDLER (lower_than, LOWER)                                           \
  BINARY_HANDLER (lower_equal, LOWER_OR_EQUAL)                                 \
  BINARY_HANDLER (greater_than, GREATER)                                       \
  BINARY_HANDLER (greater_equal, GREATER_OR_EQUAL)                             \
                                                                               \
  BINARY_HANDLER (logical_and, EN)                                             \
  BINARY_HANDLER (logical_or, ANU)                                             \
                                                                               \
  BINARY_HANDLER (array_ref, LEFT_SQUARE)                                      \
                                                                               \
  BINARY_HANDLER (field_ref, DOT)

#define BINARY_HANDLER(name, _)                                                \
  Tree binary_##name (const_TokenPtr tok, Tree left);
  BINARY_HANDLER_LIST
#undef BINARY_HANDLER

public:
  Parser (Lexer &lexer_) : lexer (lexer_), puts_fn (), printf_fn (), scanf_fn ()
  {
  }

  void parse_program ();

  Tree parse_type ();
  Tree parse_statement ();

  Tree parse_main ();
  Tree parse_function_declaration ();
  Tree parse_command_statement ();
  Tree parse_variable_statement ();
  Tree parse_o_toki_statement ();
  Tree parse_o_toki_parameter ();
  Tree parse_block_statement ();

  Tree parse_expression ();
  Tree parse_expression_naming_variable ();
  Tree parse_boolean_expression ();
  Tree parse_integer_expression ();

private:
  Lexer &lexer;
  Scope scope;

  tree main_fndecl;

  Tree puts_fn;
  Tree printf_fn;
  Tree scanf_fn;

  std::vector<TreeStmtList> stack_stmt_list;
  std::vector<TreeChain> stack_var_decl_chain;

  std::vector<BlockChain> stack_block_chain;
};

void
Parser::skip_after_eol ()
{
  const_TokenPtr t = lexer.peek_token ();

  while (t->get_id () != Toki::END_OF_FILE && t->get_id () != Toki::EOL)
    {
      lexer.skip_token ();
      t = lexer.peek_token ();
    }

  if (t->get_id () == Toki::EOL)
    lexer.skip_token ();
}

const_TokenPtr
Parser::expect_token_skip_eol (Toki::TokenId token_id)
{
  const_TokenPtr t = lexer.peek_token ();
  while (t->get_id () != Toki::END_OF_FILE && t->get_id () == Toki::EOL)
    {
      lexer.skip_token ();
      t = lexer.peek_token ();
    }

  return expect_token (token_id);
}

const_TokenPtr
Parser::expect_token (Toki::TokenId token_id)
{
  const_TokenPtr t = lexer.peek_token ();
  if (t->get_id () == token_id)
    {
      lexer.skip_token ();
      return t;
    }
  else
    {
      error_at (t->get_locus (), "expecting %s but %s found",
		get_token_description (token_id), t->get_token_description ());
      return const_TokenPtr ();
    }
}

bool
Parser::skip_token (Toki::TokenId token_id)
{
  return expect_token (token_id) != const_TokenPtr();
}

void
Parser::skip_eol ()
{
  const_TokenPtr t = lexer.peek_token ();
  while (t->get_id () != Toki::END_OF_FILE && t->get_id () == Toki::EOL)
    {
      lexer.skip_token ();
      t = lexer.peek_token ();
    }
}

void
Parser::unexpected_token (const_TokenPtr t)
{
  ::error_at (t->get_locus (), "unexpected %s", t->get_token_description ());
}

void
Parser::parse_program ()
{
  // Built type of main "int (int, char**)"
  tree main_fndecl_type_param[] = {
    integer_type_node,					     /* int */
    build_pointer_type (build_pointer_type (char_type_node)) /* char** */
  };
  tree main_fndecl_type
    = build_function_type_array (integer_type_node, 2, main_fndecl_type_param);
  // Create function declaration "int main(int, char**)"
  main_fndecl = build_fn_decl ("main", main_fndecl_type);

  // Enter top level scope
  enter_scope ();
  // program -> statement*
  parse_statement_seq (&Parser::done_end_of_file);

  // Append "return 0;"
  tree resdecl
    = build_decl (UNKNOWN_LOCATION, RESULT_DECL, NULL_TREE, integer_type_node);
  DECL_CONTEXT (resdecl) = main_fndecl;
  DECL_RESULT (main_fndecl) = resdecl;
  tree set_result
    = build2 (INIT_EXPR, void_type_node, DECL_RESULT (main_fndecl),
	      build_int_cst_type (integer_type_node, 0));
  tree return_stmt = build1 (RETURN_EXPR, void_type_node, set_result);

  get_current_stmt_list ().append (return_stmt);

  // Leave top level scope, get its binding expression and its main block
  TreeSymbolMapping main_tree_scope = leave_scope ();
  Tree main_block = main_tree_scope.block;

  // Finish main function
  BLOCK_SUPERCONTEXT (main_block.get_tree ()) = main_fndecl;
  DECL_INITIAL (main_fndecl) = main_block.get_tree ();
  DECL_SAVED_TREE (main_fndecl) = main_tree_scope.bind_expr.get_tree ();

  DECL_EXTERNAL (main_fndecl) = 0;
  DECL_PRESERVE_P (main_fndecl) = 1;

  // Convert from GENERIC to GIMPLE
  gimplify_function_tree (main_fndecl);

  // Insert it into the graph
  cgraph_node::finalize_function (main_fndecl, true);

  main_fndecl = NULL_TREE;
}

bool
Parser::done_end_of_file ()
{
  const_TokenPtr t = lexer.peek_token ();
  return (t->get_id () == Toki::END_OF_FILE);
}

bool
Parser::done_right_brace ()
{
  const_TokenPtr t = lexer.peek_token ();
  return (t->get_id () == Toki::RIGHT_BRACE);
}

void
Parser::parse_statement_seq (bool (Parser::*done) ())
{
  // Parse statements until done and append to the current stmt list;
  while (!(this->*done) ())
    {
      Tree stmt = parse_statement ();
      get_current_stmt_list ().append (stmt);
    }
}

void
Parser::enter_scope ()
{
  scope.push_scope ();

  TreeStmtList stmt_list;
  stack_stmt_list.push_back (stmt_list);

  stack_var_decl_chain.push_back (TreeChain ());
  stack_block_chain.push_back (BlockChain ());
}

Parser::TreeSymbolMapping
Parser::leave_scope ()
{
  TreeStmtList current_stmt_list = get_current_stmt_list ();
  stack_stmt_list.pop_back ();

  TreeChain var_decl_chain = stack_var_decl_chain.back ();
  stack_var_decl_chain.pop_back ();

  BlockChain subblocks = stack_block_chain.back ();
  stack_block_chain.pop_back ();

  tree new_block
    = build_block (var_decl_chain.first.get_tree (),
		   subblocks.first.get_tree (),
		   /* supercontext */ NULL_TREE, /* chain */ NULL_TREE);

  // Add the new block to the current chain of blocks (if any)
  if (!stack_block_chain.empty ())
    {
      stack_block_chain.back ().append (new_block);
    }

  // Set the subblocks to have the new block as their parent
  for (tree it = subblocks.first.get_tree (); it != NULL_TREE;
       it = BLOCK_CHAIN (it))
    BLOCK_SUPERCONTEXT (it) = new_block;

  tree bind_expr
    = build3 (BIND_EXPR, void_type_node, var_decl_chain.first.get_tree (),
	      current_stmt_list.get_tree (), new_block);

  TreeSymbolMapping tree_scope;
  tree_scope.bind_expr = bind_expr;
  tree_scope.block = new_block;

  scope.pop_scope();

  return tree_scope;
}

TreeStmtList &
Parser::get_current_stmt_list ()
{
  return stack_stmt_list.back ();
}

Tree
Parser::parse_statement ()
{
  /*
    statement ->
	   | nasin -> function_declaration
	   | o -> command:
	      1. o sin e ni BLOCK -> loop
	      2. o pana e EXPR    -> return
	      3. o IDENT          -> function call
	   */
  const_TokenPtr t = lexer.peek_token ();

  switch (t->get_id ())
    {
    case Toki::EOL:
      lexer.skip_token ();
      return NULL_TREE;
      break;
    case Toki::NASIN:
      return parse_function_declaration ();
      break;
    case Toki::O:
      return parse_command_statement ();
      break;
    case Toki::NIMI:
      return parse_variable_statement ();
      break;
    default:
      unexpected_token (t);
      return Tree::error ();
      break;
    }

  gcc_unreachable ();
}

namespace
{

bool
is_string_type (Tree type)
{
  gcc_assert (TYPE_P (type.get_tree ()));
  return type.get_tree_code () == POINTER_TYPE
	 && TYPE_MAIN_VARIANT (TREE_TYPE (type.get_tree ())) == char_type_node;
}

bool
is_array_type (Tree type)
{
  gcc_assert (TYPE_P (type.get_tree ()));
  return type.get_tree_code () == ARRAY_TYPE;
}

bool
is_record_type (Tree type)
{
  gcc_assert (TYPE_P (type.get_tree ()));
  return type.get_tree_code () == RECORD_TYPE;
}

}

Tree
Parser::parse_function_declaration ()
{
  // function declaration:
  // nasin li nimi IDENT
  // TODO:
  // parameters: li kama jo (e TYPE)+
  // return: li pana e (TYPE)+

  if (!skip_token (Toki::NASIN))
    {
      skip_after_eol ();
      return Tree::error ();
    }

  expect_token (Toki::LI);
  expect_token (Toki::NIMI);

  // Main function
  const_TokenPtr t = lexer.peek_token ();
  if (t->get_id () == Toki::OPEN)
    {
      skip_token (Toki::OPEN);
      return parse_main ();
    }

  const_TokenPtr identifier = expect_token (Toki::IDENTIFIER);

  // TODO: support parameters and return definition
  // TODO: store function somewhere to be called
  Tree block = parse_block_statement ();
  return NULL_TREE;
}

Tree
Parser::parse_command_statement ()
{
  // function call:
  // o IDENT
  if (!skip_token (Toki::O))
    {
      skip_after_eol ();
      return Tree::error ();
    }

  const_TokenPtr t = lexer.peek_token ();
  if (t->get_id () == Toki::TOKI)
    return parse_o_toki_statement ();

  // TODO: parse identifer, normal function call
  gcc_unreachable ();
}

Tree
Parser::parse_o_toki_statement ()
{
  // print function call:
  // toki e EXPR e EXPR ...
  if (!skip_token (Toki::TOKI))
    {
      skip_after_eol ();
      return Tree::error ();
    }

  const_TokenPtr next_tk = lexer.peek_token ();
  while (next_tk->get_id () != Toki::EOL)
    {
      expect_token (Toki::E);
      Tree stmt = parse_o_toki_parameter ();
      get_current_stmt_list ().append (stmt);
      next_tk = lexer.peek_token ();
    }

  return NULL_TREE;
}

Tree
Parser::parse_o_toki_parameter ()
{
  const_TokenPtr first_of_expr = lexer.peek_token ();
  Tree expr = parse_expression ();

  if (expr.is_error ())
    return Tree::error ();

  if (expr.get_type () == integer_type_node)
    {
      // printf("%d", expr)
      const char *format_integer = "%d";
      tree args[]
	= {build_string_literal (strlen (format_integer) + 1, format_integer),
	   expr.get_tree ()};

      Tree printf_fn = get_printf_addr ();

      tree stmt
	= build_call_array_loc (first_of_expr->get_locus (), integer_type_node,
				printf_fn.get_tree (), 2, args);

      return stmt;
    }
  else if (expr.get_type () == float_type_node)
    {
      // printf("%f", (double)expr)
      const char *format_float = "%f";
      tree args[]
	= {build_string_literal (strlen (format_float) + 1, format_float),
	   convert (double_type_node, expr.get_tree ())};

      Tree printf_fn = get_printf_addr ();

      tree stmt
	= build_call_array_loc (first_of_expr->get_locus (), integer_type_node,
				printf_fn.get_tree (), 2, args);

      return stmt;
    }
  else if (is_string_type (expr.get_type ()))
    {
      // printf("%s", expr)
      const char *format_str = "%s";
      tree args[]
	= {build_string_literal (strlen (format_str) + 1, format_str),
	   expr.get_tree ()};

      Tree printf_fn = get_printf_addr ();

      tree stmt
	= build_call_array_loc (first_of_expr->get_locus (), integer_type_node,
				printf_fn.get_tree (), 2, args);

      return stmt;
    }
  else
    {
      error_at (first_of_expr->get_locus (),
		"value of type %s is not a valid write operand",
		print_type (expr.get_type ()));
      return Tree::error ();
    }

  gcc_unreachable ();
}

Tree
Parser::parse_block_statement ()
{
  TreeSymbolMapping block_tree_scope;
  if (!expect_token (Toki::LEFT_BRACE))
    {
      skip_after_eol ();
      return Tree::error ();
    }

  enter_scope ();

  parse_statement_seq (&Parser::done_right_brace);

  block_tree_scope = leave_scope ();

  const_TokenPtr tok = lexer.peek_token ();
  if (tok->get_id () == Toki::RIGHT_BRACE)
    {
      // Consume '}'
      expect_token (Toki::RIGHT_BRACE);
    }
  else
    {
      unexpected_token (tok);
      return Tree::error ();
    }

  return block_tree_scope.bind_expr;
}

Tree
Parser::parse_variable_statement ()
{
  // variable definition:
  // nimi IDENT li expr
  if (!skip_token (Toki::NIMI))
    {
      skip_after_eol ();
      return Tree::error ();
    }

  const_TokenPtr identifier = expect_token (Toki::IDENTIFIER);
  if (identifier == NULL)
    {
      skip_after_eol ();
      return Tree::error ();
    }

  const_TokenPtr assig_tok = expect_token (Toki::LI);

  const_TokenPtr first_of_expr = lexer.peek_token ();
  Tree expr = parse_expression ();
  if (expr.is_error ())
    return Tree::error ();

  SymbolPtr sym = scope.get_current_mapping ().get (identifier->get_str ());
  // Add to scope if it's not there
  if (!sym)
    {
      SymbolPtr sym (new Symbol (Toki::VARIABLE, identifier->get_str ()));
      scope.get_current_mapping ().insert (sym);

      // Add variable declaration
      // TODO
      Tree type_tree = expr.get_type ();
      Tree decl = build_decl (identifier->get_locus (), VAR_DECL,
			      get_identifier (sym->get_name ().c_str ()),
			      type_tree.get_tree ());
      DECL_CONTEXT (decl.get_tree()) = main_fndecl;
      gcc_assert (!stack_var_decl_chain.empty ());
      stack_var_decl_chain.back ().append (decl);

      sym->set_tree_decl (decl);

      Tree stmt
	= build_tree (DECL_EXPR, identifier->get_locus (), void_type_node, decl);

      get_current_stmt_list ().append (stmt);
    }

  // assignment
  SymbolPtr s = scope.get_current_mapping ().get (identifier->get_str ());
  Tree variable = Tree (s->get_tree_decl (), identifier->get_locus ());
  if (variable.is_error ())
    return Tree::error ();

  if (variable.get_type () != expr.get_type ())
    {
      error_at (first_of_expr->get_locus (),
		"cannot assign value of type %s to a variable of type %s",
		print_type (expr.get_type ()),
		print_type (variable.get_type ()));
      return Tree::error ();
    }

  Tree assig_expr = build_tree (MODIFY_EXPR, assig_tok->get_locus (),
				void_type_node, variable, expr);

  return assig_expr;
}

// This is a Pratt parser
Tree
Parser::parse_expression (int right_binding_power)
{
  const_TokenPtr current_token = lexer.peek_token ();
  lexer.skip_token ();

  Tree expr = null_denotation (current_token);

  if (expr.is_error ())
    return Tree::error ();

  while (right_binding_power < left_binding_power (lexer.peek_token ()))
    {
      current_token = lexer.peek_token();
      lexer.skip_token ();

      expr = left_denotation (current_token, expr);
      if (expr.is_error ())
	return Tree::error ();
    }

  return expr;
}

namespace
{
enum binding_powers
{
  // Highest priority
  LBP_HIGHEST = 100,

  LBP_DOT = 90,

  LBP_ARRAY_REF = 80,

  LBP_UNARY_PLUS = 50,  // Used only when the null denotation is +
  LBP_UNARY_MINUS = LBP_UNARY_PLUS, // Used only when the null denotation is -

  LBP_MUL = 40,
  LBP_DIV = LBP_MUL,
  LBP_MOD = LBP_MUL,

  LBP_PLUS = 30,
  LBP_MINUS = LBP_PLUS,

  LBP_EQUAL = 20,
  LBP_DIFFERENT = LBP_EQUAL,
  LBP_LOWER_THAN = LBP_EQUAL,
  LBP_LOWER_EQUAL = LBP_EQUAL,
  LBP_GREATER_THAN = LBP_EQUAL,
  LBP_GREATER_EQUAL = LBP_EQUAL,

  LBP_LOGICAL_AND = 10,
  LBP_LOGICAL_OR = LBP_LOGICAL_AND,
  LBP_LOGICAL_NOT = LBP_LOGICAL_AND,

  // Lowest priority
  LBP_LOWEST = 0,
};
}

// This implements priorities
int
Parser::left_binding_power (const_TokenPtr token)
{
  switch (token->get_id ())
    {
    case Toki::DOT:
      return LBP_DOT;
    //
    case Toki::LEFT_SQUARE:
      return LBP_ARRAY_REF;
    //
    case Toki::ASTERISK:
      return LBP_MUL;
    case Toki::SLASH:
      return LBP_DIV;
    case Toki::PERCENT:
      return LBP_MOD;
    //
    case Toki::PLUS:
      return LBP_PLUS;
    case Toki::MINUS:
      return LBP_MINUS;
    //
    case Toki::EQUAL:
      return LBP_EQUAL;
    case Toki::DIFFERENT:
      return LBP_DIFFERENT;
    case Toki::GREATER:
      return LBP_GREATER_THAN;
    case Toki::GREATER_OR_EQUAL:
      return LBP_GREATER_EQUAL;
    case Toki::LOWER:
      return LBP_LOWER_THAN;
    case Toki::LOWER_OR_EQUAL:
      return LBP_LOWER_EQUAL;
    //
    case Toki::ANU:
      return LBP_LOGICAL_OR;
    case Toki::EN:
      return LBP_LOGICAL_AND;
    case Toki::ALA:
      return LBP_LOGICAL_NOT;
    // Anything that cannot appear after a left operand
    // is considered a terminator
    default:
      return LBP_LOWEST;
    }
}

// This is invoked when a token (including prefix operands) is found at a
// "prefix" position
Tree
Parser::null_denotation (const_TokenPtr tok)
{
  switch (tok->get_id ())
    {
    case Toki::IDENTIFIER:
      {
	SymbolPtr s = query_variable (tok->get_str (), tok->get_locus ());
	if (s == NULL)
	  return Tree::error ();
	return Tree (s->get_tree_decl (), tok->get_locus ());
      }
    case Toki::INTEGER_LITERAL:
      // FIXME : check ranges
      return Tree (build_int_cst_type (integer_type_node,
				       atoi (tok->get_str ().c_str ())),
		   tok->get_locus ());
      break;
    case Toki::REAL_LITERAL:
      {
	REAL_VALUE_TYPE real_value;
	real_from_string3 (&real_value, tok->get_str ().c_str (),
			   TYPE_MODE (float_type_node));

	return Tree (build_real (float_type_node, real_value),
		     tok->get_locus ());
      }
      break;
    case Toki::STRING_LITERAL:
      {
	std::string str = tok->get_str ();
	const char *c_str = str.c_str ();
	return Tree (build_string_literal (::strlen (c_str) + 1, c_str),
		     tok->get_locus ());
      }
      break;
    case Toki::PINI:
      {
	// new line
	const char *c_str = "\n";
	return Tree (build_string_literal (::strlen (c_str) + 1, c_str),
		     tok->get_locus ());
      }
      break;
    case Toki::LON:
      {
	// False: lon ala
	const_TokenPtr next = lexer.peek_token ();
	if (next->get_id () == Toki::ALA)
	  {
	    return Tree (build_int_cst_type (boolean_type_node, 0),
			 next->get_locus ());
	  }
	// True
	return Tree (build_int_cst_type (boolean_type_node, 1),
		     tok->get_locus ());
      }
      break;
    case Toki::LEFT_PAREN:
      {
	Tree expr = parse_expression ();
	tok = lexer.peek_token ();
	if (tok->get_id () != Toki::RIGHT_PAREN)
	  error_at (tok->get_locus (), "expecting ) but %s found",
		    tok->get_token_description ());
	else
	  lexer.skip_token ();
	return Tree (expr, tok->get_locus ());
      }
    case Toki::PLUS:
      {
	Tree expr = parse_expression (LBP_UNARY_PLUS);
	if (expr.is_error ())
	  return Tree::error ();
	if (expr.get_type () != integer_type_node
	    || expr.get_type () != float_type_node)
	  {
	    error_at (tok->get_locus (),
		      "operand of unary plus must be int or float but it is %s",
		      print_type (expr.get_type ()));
	    return Tree::error ();
	  }
	return Tree (expr, tok->get_locus ());
      }
    case Toki::MINUS:
      {
	Tree expr = parse_expression (LBP_UNARY_MINUS);
	if (expr.is_error ())
	  return Tree::error ();

	if (expr.get_type () != integer_type_node
	    || expr.get_type () != float_type_node)
	  {
	    error_at (
	      tok->get_locus (),
	      "operand of unary minus must be int or float but it is %s",
	      print_type (expr.get_type ()));
	    return Tree::error ();
	  }

	expr
	  = build_tree (NEGATE_EXPR, tok->get_locus (), expr.get_type (), expr);
	return expr;
      }
    case Toki::ALA:
      {
	Tree expr = parse_expression (LBP_LOGICAL_NOT);
	if (expr.is_error ())
	  return Tree::error ();

	if (expr.get_type () != boolean_type_node)
	  {
	    error_at (tok->get_locus (),
		      "operand of logical not must be a boolean but it is %s",
		      print_type (expr.get_type ()));
	    return Tree::error ();
	  }

	expr = build_tree (TRUTH_NOT_EXPR, tok->get_locus (), boolean_type_node,
			   expr);
	return expr;
      }
    default:
      unexpected_token (tok);
      return Tree::error ();
    }
}

Tree
Parser::coerce_binary_arithmetic (const_TokenPtr tok, Tree *left, Tree *right)
{
  Tree left_type = left->get_type ();
  Tree right_type = right->get_type ();

  if (left_type.is_error () || right_type.is_error ())
    return Tree::error ();

  if (left_type == right_type)
    {
      if (left_type == integer_type_node || left_type == float_type_node)
	{
	  return left_type;
	}
    }
  else if ((left_type == integer_type_node && right_type == float_type_node)
	   || (left_type == float_type_node && right_type == integer_type_node))
    {
      // We will coerce the integer into a float
      if (left_type == integer_type_node)
	{
	  *left = build_tree (FLOAT_EXPR, left->get_locus (), float_type_node,
			      left->get_tree ());
	}
      else
	{
	  *right = build_tree (FLOAT_EXPR, right->get_locus (),
			       float_type_node, right->get_tree ());
	}
      return float_type_node;
    }

  // i.e. int + boolean
  error_at (tok->get_locus (),
	    "invalid operands of type %s and %s for operator %s",
	    print_type (left_type), print_type (right_type),
	    tok->get_token_description ());
  return Tree::error ();
}

Parser::BinaryHandler
Parser::get_binary_handler (TokenId id)
{
  switch (id)
    {
#define BINARY_HANDLER(name, token_id)                                         \
  case Toki::token_id:                                                         \
    return &Parser::binary_##name;
      BINARY_HANDLER_LIST
#undef BINARY_HANDLER
    default:
      return NULL;
    }
}

Tree
Parser::binary_plus (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_PLUS);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (PLUS_EXPR, tok->get_locus (), tree_type, left, right);
}

Tree
Parser::binary_minus (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_MINUS);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (MINUS_EXPR, tok->get_locus (), tree_type, left, right);
}

Tree
Parser::binary_mult (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_MUL);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (MULT_EXPR, tok->get_locus (), tree_type, left, right);
}

Tree
Parser::binary_div (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_DIV);
  if (right.is_error ())
    return Tree::error ();

  if (left.get_type () == integer_type_node
      && right.get_type () == integer_type_node)
    {
      // Integer division (truncating, like in C)
      return build_tree (TRUNC_DIV_EXPR, tok->get_locus (), integer_type_node,
			 left, right);
    }
  else
    {
      // Real division
      Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
      if (tree_type.is_error ())
	return Tree::error ();

      gcc_assert (tree_type == float_type_node);

      return build_tree (RDIV_EXPR, tok->get_locus (), tree_type, left, right);
    }
}

Tree
Parser::binary_mod (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_MOD);
  if (right.is_error ())
    return Tree::error ();

  if (left.get_type () == integer_type_node
      && right.get_type () == integer_type_node)
    {
      // Integer division (truncating, like in C)
      return build_tree (TRUNC_MOD_EXPR, tok->get_locus (), integer_type_node,
			 left, right);
    }
  else
    {
      error_at (tok->get_locus (),
		"operands of modulus must be of integer type");
      return Tree::error ();
    }
}

Tree
Parser::binary_equal (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_EQUAL);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (EQ_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

Tree
Parser::binary_different (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_DIFFERENT);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (NE_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

Tree
Parser::binary_lower_than (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_LOWER_THAN);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (LT_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

Tree
Parser::binary_lower_equal (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_LOWER_EQUAL);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (LE_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

Tree
Parser::binary_greater_than (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_GREATER_THAN);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (GT_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

Tree
Parser::binary_greater_equal (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_GREATER_EQUAL);
  if (right.is_error ())
    return Tree::error ();

  Tree tree_type = coerce_binary_arithmetic (tok, &left, &right);
  if (tree_type.is_error ())
    return Tree::error ();

  return build_tree (GE_EXPR, tok->get_locus (), boolean_type_node, left,
		     right);
}

bool
Parser::check_logical_operands (const_TokenPtr tok, Tree left, Tree right)
{
  if (left.get_type () != boolean_type_node
      || right.get_type () != boolean_type_node)
    {
      error_at (
	tok->get_locus (),
	"operands of operator %s must be boolean but they are %s and %s\n",
	tok->get_token_description (), print_type (left.get_type ()),
	print_type (right.get_type ()));
      return false;
    }

  return true;
}

Tree
Parser::binary_logical_and (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_LOGICAL_AND);
  if (right.is_error ())
    return Tree::error ();

  if (!check_logical_operands (tok, left, right))
    return Tree::error ();

  return build_tree (TRUTH_ANDIF_EXPR, tok->get_locus (), boolean_type_node,
		     left, right);
}

Tree
Parser::binary_logical_or (const_TokenPtr tok, Tree left)
{
  Tree right = parse_expression (LBP_LOGICAL_OR);
  if (right.is_error ())
    return Tree::error ();

  if (!check_logical_operands (tok, left, right))
    return Tree::error ();

  return build_tree (TRUTH_ORIF_EXPR, tok->get_locus (), boolean_type_node,
		     left, right);
}

Tree
Parser::binary_array_ref (const const_TokenPtr tok, Tree left)
{
  Tree right = parse_integer_expression ();
  if (right.is_error ())
    return Tree::error ();

  if (!skip_token (Toki::RIGHT_SQUARE))
    return Tree::error ();

  if (!is_array_type (left.get_type ()))
    {
      error_at (left.get_locus(), "does not have array type");
      return Tree::error ();
    }

  Tree element_type = TREE_TYPE(left.get_type().get_tree());

  return build_tree (ARRAY_REF, tok->get_locus (), element_type, left, right, Tree(), Tree());
}

Tree
Parser::binary_field_ref (const const_TokenPtr tok, Tree left)
{
  const_TokenPtr identifier = expect_token (Toki::IDENTIFIER);
  if (identifier == NULL)
    {
      return Tree::error ();
    }

  if (!is_record_type (left.get_type ()))
    {
      error_at (left.get_locus (), "does not have record type");
      return Tree::error ();
    }

  Tree field_decl = TYPE_FIELDS (left.get_type ().get_tree ());
  while (!field_decl.is_null ())
    {
      Tree decl_name = DECL_NAME (field_decl.get_tree ());
      const char *field_name = IDENTIFIER_POINTER (decl_name.get_tree ());

      if (field_name == identifier->get_str ())
	break;

      field_decl = TREE_CHAIN (field_decl.get_tree ());
    }

  if (field_decl.is_null ())
    {
      error_at (left.get_locus (),
		"record type does not have a field named '%s'",
		identifier->get_str ().c_str ());
      return Tree::error ();
    }

  return build_tree (COMPONENT_REF, tok->get_locus (),
		     TREE_TYPE (field_decl.get_tree ()), left, field_decl,
		     Tree ());
}

// This is invoked when a token (likely an operand) is found at a (likely
// infix) non-prefix position
Tree
Parser::left_denotation (const_TokenPtr tok, Tree left)
{
  BinaryHandler binary_handler = get_binary_handler (tok->get_id ());
  if (binary_handler == NULL)
    {
      unexpected_token (tok);
      return Tree::error ();
    }

  return (this->*binary_handler) (tok, left);
}

const char *
Parser::print_type (Tree type)
{
  gcc_assert (TYPE_P (type.get_tree ()));

  if (type == void_type_node)
    {
      return "void";
    }
  else if (type == integer_type_node)
    {
      return "int";
    }
  else if (type == float_type_node)
    {
      return "float";
    }
  else if (is_string_type (type))
    {
      return "string";
    }
  else if (is_array_type(type))
    {
      return "array";
    }
  else if (type == boolean_type_node)
    {
      return "boolean";
    }
  else
    {
      return "<<unknown-type>>";
    }
}

SymbolPtr
Parser::query_type (const std::string &name, location_t loc)
{
  SymbolPtr sym = scope.lookup (name);
  if (sym == NULL)
    {
      error_at (loc, "type '%s' not declared in the current scope",
		name.c_str ());
    }
  else if (sym->get_kind () != Toki::TYPENAME)
    {
      error_at (loc, "name '%s' is not a type", name.c_str ());
      sym = SymbolPtr();
    }
  return sym;
}

SymbolPtr
Parser::query_variable (const std::string &name, location_t loc)
{
  SymbolPtr sym = scope.lookup (name);
  if (sym == NULL)
    {
      error_at (loc, "variable '%s' not declared in the current scope",
		name.c_str ());
    }
  else if (sym->get_kind () != Toki::VARIABLE)
    {
      error_at (loc, "name '%s' is not a variable", name.c_str ());
      sym = SymbolPtr();
    }
  return sym;
}

SymbolPtr
Parser::query_integer_variable (const std::string &name, location_t loc)
{
  SymbolPtr sym = query_variable (name, loc);
  if (sym != NULL)
    {
      Tree var_decl = sym->get_tree_decl ();
      gcc_assert (!var_decl.is_null ());

      if (var_decl.get_type () != integer_type_node)
	{
	  error_at (loc, "variable '%s' does not have integer type",
		    name.c_str ());
	  sym = SymbolPtr();
	}
    }

  return sym;
}

Tree
Parser::get_puts_addr ()
{
  if (puts_fn.is_null ())
    {
      tree fndecl_type_param[] = {
	build_pointer_type (
	  build_qualified_type (char_type_node,
				TYPE_QUAL_CONST)) /* const char* */
      };
      tree fndecl_type
	= build_function_type_array (integer_type_node, 1, fndecl_type_param);

      tree puts_fn_decl = build_fn_decl ("puts", fndecl_type);
      DECL_EXTERNAL (puts_fn_decl) = 1;

      puts_fn
	= build1 (ADDR_EXPR, build_pointer_type (fndecl_type), puts_fn_decl);
    }

  return puts_fn;
}

Tree
Parser::get_printf_addr ()
{
  if (printf_fn.is_null ())
    {
      tree fndecl_type_param[] = {
	build_pointer_type (
	  build_qualified_type (char_type_node,
				TYPE_QUAL_CONST)) /* const char* */
      };
      tree fndecl_type
	= build_varargs_function_type_array (integer_type_node, 1,
					     fndecl_type_param);

      tree printf_fn_decl = build_fn_decl ("printf", fndecl_type);
      DECL_EXTERNAL (printf_fn_decl) = 1;

      printf_fn
	= build1 (ADDR_EXPR, build_pointer_type (fndecl_type), printf_fn_decl);
    }

  return printf_fn;
}

Tree
Parser::parse_expression ()
{
  return parse_expression (/* right_binding_power */ 0);
}

Tree
Parser::parse_boolean_expression ()
{
  Tree expr = parse_expression ();
  if (expr.is_error ())
    return expr;

  if (expr.get_type () != boolean_type_node)
    {
      error_at (expr.get_locus (),
		"expected expression of boolean type but its type is %s",
		print_type (expr.get_type ()));
      return Tree::error ();
    }
  return expr;
}

Tree
Parser::parse_integer_expression ()
{
  Tree expr = parse_expression ();
  if (expr.is_error ())
    return expr;

  if (expr.get_type () != integer_type_node)
    {
      error_at (expr.get_locus (),
		"expected expression of integer type but its type is %s",
		print_type (expr.get_type ()));
      return Tree::error ();
    }
  return expr;
}

Tree
Parser::parse_expression_naming_variable ()
{
  Tree expr = parse_expression ();
  if (expr.is_error ())
    return expr;

  if (expr.get_tree_code () != VAR_DECL && expr.get_tree_code () != ARRAY_REF
      && expr.get_tree_code () != COMPONENT_REF)
    {
      error_at (expr.get_locus (),
		"does not designate a variable, array element or field");
      return Tree::error ();
    }
  return expr;
}

Tree
Parser::parse_type ()
{
  // type -> "int"
  //      | "float"
  //      | "bool"
  //      | IDENTIFIER
  //      | type '[' expr ']'
  //      | type '(' expr : expr ')'

  const_TokenPtr t = lexer.peek_token ();

  Tree type;

  switch (t->get_id ())
    {
    case Toki::IDENTIFIER:
      {
	SymbolPtr s = query_type (t->get_str (), t->get_locus ());
	lexer.skip_token ();
	if (s == NULL)
	  type = Tree::error ();
	else
	  type = TREE_TYPE (s->get_tree_decl ().get_tree ());
      }
      break;
    default:
      unexpected_token (t);
      return Tree::error ();
      break;
    }

  typedef std::vector<std::pair<Tree, Tree> > Dimensions;
  Dimensions dimensions;

  t = lexer.peek_token ();
  while (t->get_id () == Toki::LEFT_PAREN || t->get_id () == Toki::LEFT_SQUARE)
    {
      lexer.skip_token ();

      Tree lower_bound, upper_bound;
      if (t->get_id () == Toki::LEFT_SQUARE)
	{
	  Tree size = parse_integer_expression ();
	  skip_token (Toki::RIGHT_SQUARE);

	  lower_bound = Tree (build_int_cst_type (integer_type_node, 0),
			      size.get_locus ());
	  upper_bound
	    = build_tree (MINUS_EXPR, size.get_locus (), integer_type_node,
			  size, build_int_cst (integer_type_node, 1));

	}
      else if (t->get_id () == Toki::LEFT_PAREN)
	{
	  lower_bound = parse_integer_expression ();
	  skip_token (Toki::COLON);

	  upper_bound = parse_integer_expression ();
	  skip_token (Toki::RIGHT_PAREN);
	}
      else
	{
	  gcc_unreachable ();
	}

      dimensions.push_back (std::make_pair (lower_bound, upper_bound));
      t = lexer.peek_token ();
    }

  for (Dimensions::reverse_iterator it = dimensions.rbegin ();
       it != dimensions.rend (); it++)
    {
      it->first = Tree (fold (it->first.get_tree ()), it->first.get_locus ());
//       if (it->first.get_tree_code () != INTEGER_CST)
// 	{
// 	  error_at (it->first.get_locus (), "is not an integer constant");
// 	  break;
// 	}
      it->second
	= Tree (fold (it->second.get_tree ()), it->second.get_locus ());
//       if (it->second.get_tree_code () != INTEGER_CST)
// 	{
// 	  error_at (it->second.get_locus (), "is not an integer constant");
// 	  break;
// 	}

      if (!type.is_error ())
	{
	  Tree range_type
	    = build_range_type (integer_type_node, it->first.get_tree (),
				it->second.get_tree ());
	  type = build_array_type (type.get_tree (), range_type.get_tree ());
	}
    }

  return type;
}

Tree
Parser::parse_main ()
{
  Tree main = parse_block_statement ();
  return main;
}

}

// ------------------------------------------------------
// ------------------------------------------------------
// ------------------------------------------------------

static void toki_parse_file (const char *filename);

void
toki_parse_files (int num_files, const char **files)
{
  for (int i = 0; i < num_files; i++)
    {
      toki_parse_file (files[i]);
    }
}

static void
toki_parse_file (const char *filename)
{
  // FIXME: handle stdin "-"
  FILE *file = fopen (filename, "r");
  if (file == NULL)
    {
      fatal_error (UNKNOWN_LOCATION, "cannot open filename %s: %m", filename);
    }

  Toki::Lexer lexer (filename, file);
  Toki::Parser parser (lexer);

  parser.parse_program ();

  fclose (file);
}
