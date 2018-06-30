
// ceph/src/include/types.h

#ifndef MYCC_UTIL_TEXT_TABLE_H_
#define MYCC_UTIL_TEXT_TABLE_H_

#include <assert.h>
#include <deque>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

// -- io helpers --
// Forward declare all the I/O helpers so strict ADL can find them in
// the case of containers of containers. I'm tempted to abstract this
// stuff using template templates like I did for denc.
// Koening lookup/argument-dependent lookup ?

template <class A, class B>
inline std::ostream &operator<<(std::ostream &out, const std::pair<A, B> &v);
template <class A, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::vector<A, Alloc> &v);
template <class A, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::deque<A, Alloc> &v);
template <class A, class B, class C>
inline std::ostream &operator<<(std::ostream &out, const std::tuple<A, B, C> &t);
template <class A, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::list<A, Alloc> &ilist);
template <class A, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::set<A, Comp, Alloc> &iset);
template <class A, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::multiset<A, Comp, Alloc> &iset);
template <class A, class B, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::map<A, B, Comp, Alloc> &m);
template <class A, class B, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::multimap<A, B, Comp, Alloc> &m);

template <class A, class B>
inline std::ostream &operator<<(std::ostream &out, const std::pair<A, B> &v)
{
  return out << "pair(" << v.first << ", " << v.second << ")";
}

template <class A, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::vector<A, Alloc> &v)
{
  out << "vector[";
  for (auto p = v.begin(); p != v.end(); ++p)
  {
    if (p != v.begin())
      out << ", ";
    out << *p;
  }
  out << "]";
  return out;
}

template <class A, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::deque<A, Alloc> &v)
{
  out << "deque<";
  for (auto p = v.begin(); p != v.end(); ++p)
  {
    if (p != v.begin())
      out << ", ";
    out << *p;
  }
  out << ">";
  return out;
}

template <class A, class B, class C>
inline std::ostream &operator<<(std::ostream &out, const std::tuple<A, B, C> &t)
{
  out << std::get<0>(t) << "," << std::get<1>(t) << "," << std::get<2>(t);
  return out;
}

template <class A, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::list<A, Alloc> &ilist)
{
  out << "list[";
  for (auto it = ilist.begin();
       it != ilist.end();
       ++it)
  {
    if (it != ilist.begin())
      out << ", ";
    out << *it;
  }
  out << "]";
  return out;
}

template <class A, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::set<A, Comp, Alloc> &iset)
{
  out << "set(";
  for (auto it = iset.begin();
       it != iset.end();
       ++it)
  {
    if (it != iset.begin())
      out << ", ";
    out << *it;
  }
  out << ")";
  return out;
}

template <class A, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::multiset<A, Comp, Alloc> &iset)
{
  out << "multiset(";
  for (auto it = iset.begin();
       it != iset.end();
       ++it)
  {
    if (it != iset.begin())
      out << ", ";
    out << *it;
  }
  out << ")";
  return out;
}

template <class A, class B, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::map<A, B, Comp, Alloc> &m)
{
  out << "map{";
  for (auto it = m.begin();
       it != m.end();
       ++it)
  {
    if (it != m.begin())
      out << ", ";
    out << it->first << "=" << it->second;
  }
  out << "}";
  return out;
}

template <class A, class B, class Comp, class Alloc>
inline std::ostream &operator<<(std::ostream &out, const std::multimap<A, B, Comp, Alloc> &m)
{
  out << "multimap{{";
  for (auto it = m.begin();
       it != m.end();
       ++it)
  {
    if (it != m.begin())
      out << ", ";
    out << it->first << "=" << it->second;
  }
  out << "}}";
  return out;
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
    std::string heading;
    int32_t width;
    Align hd_align;
    Align col_align;

    TextTableColumn() {}
    TextTableColumn(std::string h, int32_t w, Align ha, Align ca) : heading(h), width(w), hd_align(ha), col_align(ca) {}
    ~TextTableColumn() {}
  };

  std::vector<TextTableColumn> col; // column definitions
  uint32_t curcol, currow;          // col, row being inserted into
  uint32_t indent;                  // indent width when rendering

protected:
  std::vector<std::vector<std::string>> row; // row data array

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
  void define_column(const std::string &heading, Align hd_align,
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

#endif // MYCC_UTIL_TEXT_TABLE_H_