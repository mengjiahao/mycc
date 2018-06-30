
#ifndef MYCC_UTIL_REGISTRY_UTIL_H_
#define MYCC_UTIL_REGISTRY_UTIL_H_

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "error_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

/*!
 * \brief Registry class.
 *  Registry can be used to register global singletons.
 *  The most commonly use case are factory functions.
 *
 * \tparam EntryType Type of Registry entries,
 *     EntryType need to name a name field.
 */
template <typename EntryType>
class Registry
{
public:
  /*! \return list of entries in the registry(excluding alias) */
  inline static const std::vector<const EntryType *> &List()
  {
    return Get()->const_list_;
  }
  /*! \return list all names registered in the registry, including alias */
  inline static std::vector<string> ListAllNames()
  {
    const std::map<string, EntryType *> &fmap = Get()->fmap_;
    typename std::map<string, EntryType *>::const_iterator p;
    std::vector<string> names;
    for (p = fmap.begin(); p != fmap.end(); ++p)
    {
      names.push_back(p->first);
    }
    return names;
  }
  /*!
   * \brief Find the entry with corresponding name.
   * \param name name of the function
   * \return the corresponding function, can be NULL
   */
  inline static const EntryType *Find(const string &name)
  {
    const std::map<string, EntryType *> &fmap = Get()->fmap_;
    typename std::map<string, EntryType *>::const_iterator p = fmap.find(name);
    if (p != fmap.end())
    {
      return p->second;
    }
    else
    {
      return NULL;
    }
  }
  /*!
   * \brief Add alias to the key_name
   * \param key_name The original entry key
   * \param alias The alias key.
   */
  inline void AddAlias(const string &key_name,
                       const string &alias)
  {
    EntryType *e = fmap_.at(key_name);
    if (fmap_.count(alias))
    {
      PANIC_EQ(e, fmap_.at(alias)); // "Trying to register alias " << alias << " for key " << key_name << " but " << alias << " is already taken";
    }
    else
    {
      fmap_[alias] = e;
    }
  }
  /*!
   * \brief Internal function to register a name function under name.
   * \param name name of the function
   * \return ref to the registered entry, used to set properties
   */
  inline EntryType &__REGISTER__(const string &name)
  {
    PANIC_EQ(fmap_.count(name), 0U); // name << " already registered";
    EntryType *e = new EntryType();
    e->name = name;
    fmap_[name] = e;
    const_list_.push_back(e);
    entry_list_.push_back(e);
    return *e;
  }
  /*!
   * \brief Internal function to either register or get registered entry
   * \param name name of the function
   * \return ref to the registered entry, used to set properties
   */
  inline EntryType &__REGISTER_OR_GET__(const string &name)
  {
    if (fmap_.count(name) == 0)
    {
      return __REGISTER__(name);
    }
    else
    {
      return *fmap_.at(name);
    }
  }
  /*!
   * \brief get a singleton of the Registry.
   *  This function can be defined by MYCC_ENABLE_REGISTRY.
   * \return get a singleton
   */
  static Registry *Get();

private:
  /*! \brief list of entry types */
  std::vector<EntryType *> entry_list_;
  /*! \brief list of entry types */
  std::vector<const EntryType *> const_list_;
  /*! \brief map of name->function */
  std::map<string, EntryType *> fmap_;
  /*! \brief constructor */
  Registry() {}
  /*! \brief destructor */
  ~Registry()
  {
    for (size_t i = 0; i < entry_list_.size(); ++i)
    {
      delete entry_list_[i];
    }
  }
};

/*!
 * \brief Common base class for function registry.
 *
 * \code
 *  // This example demonstrates how to use Registry to create a factory of trees.
 *  struct TreeFactory :
 *      public FunctionRegEntryBase<TreeFactory, std::function<Tree*()> > {
 *  };
 *
 *  // in a independent cc file
 *  namespace mycc {
 *  MYCC_REGISTRY_ENABLE(TreeFactory);
 *  }
 *  // register binary tree constructor into the registry.
 *  MYCC_REGISTRY_REGISTER(TreeFactory, TreeFactory, BinaryTree)
 *      .describe("Constructor of BinaryTree")
 *      .set_body([]() { return new BinaryTree(); });
 * \endcode
 *
 * \tparam EntryType The type of subclass that inheritate the base.
 * \tparam FunctionType The function type this registry is registerd.
 */
template <typename EntryType, typename FunctionType>
class FunctionRegEntryBase
{
public:
  /*!
 * \brief Information about a parameter field in string representations.
 */
  struct ParamFieldInfo
  {
    /*! \brief name of the field */
    string name;
    /*! \brief type of the field in string format */
    string type;
    /*!
   * \brief detailed type information string
   *  This include the default value, enum constran and typename.
   */
    string type_info_str;
    /*! \brief detailed description of the type */
    string description;
  };

  /*! \brief name of the entry */
  string name;
  /*! \brief description of the entry */
  string description;
  /*! \brief additional arguments to the factory function */
  std::vector<ParamFieldInfo> arguments;
  /*! \brief Function body to create ProductType */
  FunctionType body;
  /*! \brief Return type of the function */
  string return_type;

  /*!
   * \brief Set the function body.
   * \param body Function body to set.
   * \return reference to self.
   */
  inline EntryType &set_body(FunctionType body)
  {
    this->body = body;
    return this->self();
  }
  /*!
   * \brief Describe the function.
   * \param description The description of the factory function.
   * \return reference to self.
   */
  inline EntryType &describe(const string &description)
  {
    this->description = description;
    return this->self();
  }
  /*!
   * \brief Add argument information to the function.
   * \param name Name of the argument.
   * \param type Type of the argument.
   * \param description Description of the argument.
   * \return reference to self.
   */
  inline EntryType &add_argument(const string &name,
                                 const string &type,
                                 const string &description)
  {
    ParamFieldInfo info;
    info.name = name;
    info.type = type;
    info.type_info_str = info.type;
    info.description = description;
    arguments.push_back(info);
    return this->self();
  }
  /*!
   * \brief Append list if arguments to the end.
   * \param args Additional list of arguments.
   * \return reference to self.
   */
  inline EntryType &add_arguments(const std::vector<ParamFieldInfo> &args)
  {
    arguments.insert(arguments.end(), args.begin(), args.end());
    return this->self();
  }
  /*!
  * \brief Set the return type.
  * \param type Return type of the function, could be Symbol or Symbol[]
  * \return reference to self.
  */
  inline EntryType &set_return_type(const string &type)
  {
    return_type = type;
    return this->self();
  }

protected:
  /*!
   * \return reference of self as derived type
   */
  inline EntryType &self()
  {
    return *(static_cast<EntryType *>(this));
  }
};

/*!
 * \def MYCC_REGISTRY_ENABLE
 * \brief Macro to enable the registry of EntryType.
 * This macro must be used under namespace MYCC, and only used once in cc file.
 * \param EntryType Type of registry entry
 */
#define MYCC_REGISTRY_ENABLE(EntryType)           \
  template <>                                     \
  Registry<EntryType> *Registry<EntryType>::Get() \
  {                                               \
    static Registry<EntryType> inst;              \
    return &inst;                                 \
  }

/*!
 * \brief Generic macro to register an EntryType
 *  There is a complete example in FactoryRegistryEntryBase.
 *
 * \param EntryType The type of registry entry.
 * \param EntryTypeName The typename of EntryType, must do not contain namespace :: .
 * \param Name The name to be registered.
 * \sa FactoryRegistryEntryBase
 */
#define MYCC_REGISTRY_REGISTER(EntryType, EntryTypeName, Name)             \
  static ATTRIBUTE_UNUSED EntryType &__make_##EntryTypeName##_##Name##__ = \
      ::mycc::util::Registry<EntryType>::Get()->__REGISTER__(#Name)

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_REGISTRY_UTIL_H_