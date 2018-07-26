
#ifndef MYCC_UTIL_PRETTY_PRINT_H_
#define MYCC_UTIL_PRETTY_PRINT_H_

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

namespace Color
{

// Color table
enum Code
{
  /*********************************************************
 *  Color for front                                      *
 *********************************************************/
  FG_RED = 31,
  FG_GREEN = 32,
  FG_YELLOW = 33,
  FG_BLUE = 34,
  FG_MAGENTA = 35,
  FG_CYAN = 36,
  FG_WHITE = 37,
  FG_DEFAULT = 39,
  /*********************************************************
 *  Color for Background                                 *
 *********************************************************/
  BG_RED = 41,
  BG_GREEN = 42,
  BG_YELLOW = 43,
  BG_BLUE = 44,
  BG_MAGENTA = 45,
  BG_CYAN = 46,
  BG_WHITE = 47,
  BG_DEFAULT = 49,
  /*********************************************************
 *  Control code                                         *
 *********************************************************/
  RESET = 0, // everything back to normal
  BOLD = 1,  // often a brighter shade of the same colour
  UNDER_LINE = 4,
  INVERSE = 7, // swap forground and background color
  BOLD_OFF = 21,
  UNDER_LINE_OFF = 24,
  INVERSE_OFF = 27
};

//------------------------------------------------------------------------------
// The Modifier class can be used to set the foreground and background
// color of output. We can this class like this:
//
//   Modifier red(Color::FG_RED);
//   Modifier def(Color::FG_DEFAULT);
//   cout << "This ->" << red << "word" << def "<- is red. " << endl;
//------------------------------------------------------------------------------
class Modifier
{
  Code code;

public:
  Modifier(Code pCode) : code(pCode) {}
  friend std::ostream &
  operator<<(std::ostream &os, const Modifier &mod)
  {
    return os << "\033[" << mod.code << "m";
  }

private:
  DISALLOW_COPY_AND_ASSIGN(Modifier);
};

} // namespace Color

// [Warning] blablabla ...
inline void print_warning(const string &out)
{
  Color::Modifier mag(Color::FG_MAGENTA);
  Color::Modifier bold(Color::BOLD);
  Color::Modifier reset(Color::RESET);
  std::cout << mag << bold << "[ WARNING    ] "
            << out << reset << std::endl;
}

inline void print_error(const string &out)
{
  Color::Modifier red(Color::FG_RED);
  Color::Modifier bold(Color::BOLD);
  Color::Modifier reset(Color::RESET);
  std::cout << red << bold << "[ ERROR      ] "
            << out << reset << std::endl;
}

inline void print_action(const string &out)
{
  Color::Modifier green(Color::FG_GREEN);
  Color::Modifier bold(Color::BOLD);
  Color::Modifier reset(Color::RESET);
  std::cout << green << bold << "[ ACTION     ] "
            << out << reset << std::endl;
}

inline void print_info(const string &out, bool imp = false)
{
  Color::Modifier green(Color::FG_GREEN);
  Color::Modifier bold(Color::BOLD);
  Color::Modifier reset(Color::RESET);
  if (!imp)
  {
    std::cout << green << "[------------] " << reset
              << out << std::endl;
  }
  else
  {
    std::cout << green << bold << "[------------] " << out << std::endl;
  }
}

//------------------------------------------------------------------------------
// Example:
//
//  column ->  "Name", "ID", "Count", "Price"
//  width -> 10, 10, 10, 10
//
// Output:
//
//   Name       ID        Count   Price
//   Fruit      0x101       50     5.27
//   Juice      0x102       20     8.73
//   Meat       0x104       30    10.13
//------------------------------------------------------------------------------
template <typename T>
void print_row(const std::vector<T> &column,
               const std::vector<int> &width)
{
  assert(column.size() == width.size());
  for (size_t i = 0; i < column.size(); ++i)
  {
    std::cout.width(width[i]);
    std::cout << column[i];
  }
  std::cout << "\n";
}

/**
 * TextTable:
 * Manage tabular output of data.  Caller defines heading of each column
 * and alignment of heading and column data,
 * then inserts rows of data including tuples of
 * length (ncolumns) terminated by TextTable::endrow.  When all rows
 * are inserted, caller asks for output with ostream <<
 * which sizes/pads/dumps the table to ostream.
 *
 * Columns autosize to largest heading or datum.  One space is printed
 * between columns.
 * 
 * use like:
 * tbl.define_column("NAME", TextTable::LEFT, TextTable::LEFT);
 * tbl.define_column("ID", TextTable::LEFT, TextTable::LEFT);
 * tbl << stringify(si_t(osd_sum.kb*1024)) << percentify(used*100) << TextTable::endrow;
 * tbl.set_indent(4); stringstream ss << tbl;
 */

class TextTable
{

public:
  enum Align
  {
    LEFT = 1,
    CENTER,
    RIGHT
  };

private:
  struct TextTableColumn
  {
    string heading;
    int32_t width;
    Align hd_align;
    Align col_align;

    TextTableColumn() {}
    TextTableColumn(string h, int32_t w, Align ha, Align ca)
        : heading(h), width(w), hd_align(ha), col_align(ca) {}
    ~TextTableColumn() {}
  };

  std::vector<TextTableColumn> col; // column definitions
  uint32_t curcol, currow;          // col, row being inserted into
  uint32_t indent;                  // indent width when rendering

protected:
  std::vector<std::vector<string>> row; // row data array

public:
  TextTable() : curcol(0), currow(0), indent(0) {}
  ~TextTable() {}

  /**
   * Define a column in the table.
   *
   * @param heading Column heading string (or "")
   * @param hd_align Alignment for heading in column
   * @param col_align Data alignment
   *
   * @note alignment is of type TextTable::Align; values are
   * TextTable::LEFT, TextTable::CENTER, or TextTable::RIGHT
   *
   */
  void define_column(const string &heading, Align hd_align,
                     Align col_align);

  /**
   * Set indent for table.  Only affects table output.
   *
   * @param i Number of spaces to indent
   */
  void set_indent(int i) { indent = i; }

  /**
   * Add item to table, perhaps on new row.
   * table << val1 << val2 << TextTable::endrow;
   *
   * @param: value to output.
   *
   * @note: Numerics are output in decimal; strings are not truncated.
   * Output formatting choice is limited to alignment in define_column().
   *
   * @return TextTable& for chaining.
   */

  template <typename T>
  TextTable &operator<<(const T &item)
  {
    if (row.size() < currow + 1)
      row.resize(currow + 1);

    /**
     * col.size() is a good guess for how big row[currow] needs to be,
     * so just expand it out now
     */
    if (row[currow].size() < col.size())
    {
      row[currow].resize(col.size());
    }

    // inserting more items than defined columns is a coding error
    assert(curcol + 1 <= col.size());

    // get rendered width of item alone
    std::ostringstream oss;
    oss << item;
    int width = oss.str().length();
    oss.seekp(0);

    // expand column width if necessary
    if (width > col[curcol].width)
    {
      col[curcol].width = width;
    }

    // now store the rendered item with its proper width
    row[currow][curcol] = oss.str();

    curcol++;
    return *this;
  }

  /**
   * Degenerate type/variable here is just to allow selection of the
   * following operator<< for "<< TextTable::endrow"
   */

  struct endrow_t
  {
  };
  static endrow_t endrow;

  /**
   * Implements TextTable::endrow
   */

  TextTable &operator<<(endrow_t)
  {
    curcol = 0;
    currow++;
    return *this;
  }

  /**
   * Render table to ostream (i.e. cout << table)
   */

  friend std::ostream &operator<<(std::ostream &out, const TextTable &t);

  /**
   * clear: Reset everything in a TextTable except column defs
   * resize cols to heading widths, clear indent
   */

  void clear();
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_PRETTY_PRINT_H_