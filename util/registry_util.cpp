
#include "registry_util.h"
#include <stdio.h>
#include <functional>
#include "testharness.h"

namespace tree
{

struct Tree
{
  virtual void Print() = 0;
  virtual ~Tree() {}
};

struct BinaryTree : public Tree
{
  virtual void Print()
  {
    PRINT_INFO("%s\n", "I am binary tree");
  }
};

struct AVLTree : public Tree
{
  virtual void Print()
  {
    PRINT_INFO("%s\n", "I am AVL tree\n");
  }
};

// registry to get the trees
struct TreeFactory
    : public ::mycc::util::FunctionRegEntryBase<TreeFactory, std::function<Tree *()>>
{
};

#define REGISTER_TREE(Name)                                      \
  MYCC_REGISTRY_REGISTER(::tree::TreeFactory, TreeFactory, Name) \
      .set_body([]() { return new Name(); })

} // namespace tree

// usually this sits on a seperate file
namespace mycc
{
namespace util
{
MYCC_REGISTRY_ENABLE(tree::TreeFactory);
}
} // namespace mycc

// Register the trees, can be in seperate files
namespace tree
{

REGISTER_TREE(BinaryTree)
    .describe("This is a binary tree.");

REGISTER_TREE(AVLTree);

} // namespace tree

int main(int argc, char *argv[])
{
  // construct a binary tree
  tree::Tree *binary = ::mycc::util::Registry<tree::TreeFactory>::Find("BinaryTree")->body();
  binary->Print();
  // construct a binary tree
  tree::Tree *avl = ::mycc::util::Registry<tree::TreeFactory>::Find("AVLTree")->body();
  avl->Print();
  delete binary;
  delete avl;
  return 0;
}