/// \file ROOT/RTreeField.hxx
/// \ingroup Forest ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2018-10-09
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2015, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_RTreeField
#define ROOT7_RTreeField

#include <ROOT/RColumn.hxx>
#include <ROOT/RColumnElement.hxx>
#include <ROOT/RForestUtil.hxx>
#include <ROOT/RStringView.hxx>
#include <ROOT/RTreeValue.hxx>
#include <ROOT/TypeTraits.hxx>

#include <TError.h>

#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <utility>

namespace ROOT {
namespace Experimental {

namespace Detail {

class RTreeValueBase;
class RPageStorage;

// clang-format off
/**
\class ROOT::Experimental::RTreeFieldBase
\ingroup Forest
\brief A field translates read and write calls from/to underlying columns to/from tree values

A field is a serializable C++ type or a container for a collection of sub fields. The RTreeFieldBase and its
type-safe descendants provide the object to column mapper. They map C++ objects to primitive columns.  The
mapping is trivial for simple types such as 'double'. Complex types resolve to multiple primitive columns.
The field knows based on its type and the field name the type(s) and name(s) of the columns.
*/
// clang-format on
class RTreeFieldBase {
private:
   /// The field name is a unique within a tree and also the basis for the column name(s)
   std::string fName;
   /// The C++ type captured by this field
   std::string fType;
   /// A field on a trivial type that maps as-is to a single column
   bool fIsSimple;
   /// Collections own sub fields
   std::vector<std::unique_ptr<RTreeFieldBase>> fSubFields;
   /// Sub fields point to their mother field
   RTreeFieldBase* fParent;

protected:
   /// All fields have a main column. For collection fields, the main column is the index field. Points into fColumns.
   RColumn* fPrincipalColumn;
   /// The columns are connected either to a sink or to a source (not to both); they are owned by the field.
   std::vector<std::unique_ptr<RColumn>> fColumns;

   /// Creates the backing columns corresponsing to the field type and name
   virtual void DoGenerateColumns() = 0;

   /// Operations on values of complex types, e.g. ones that involve multiple columns or for which no direct
   /// column type exists.
   virtual void DoAppend(const RTreeValueBase& value);
   virtual void DoRead(TreeIndex_t index, RTreeValueBase* value);
   virtual void DoReadV(TreeIndex_t index, TreeIndex_t count, void* dst);

public:
   /// Field names convey the level of subfields; sub fields (nested collections) are separated by a dot
   static constexpr char kLevelSeparator = '.';

   /// Iterates over the sub fields in depth-first search order
   class RIterator : public std::iterator<std::forward_iterator_tag, Detail::RTreeFieldBase> {
   private:
      using iterator = RIterator;
      struct Position {
         Position() : fFieldPtr(nullptr), fIdxInParent(-1) { }
         Position(pointer fieldPtr, int idxInParent) : fFieldPtr(fieldPtr), fIdxInParent(idxInParent) { }
         pointer fFieldPtr;
         int fIdxInParent;
      };
      /// The stack of nodes visited when walking down the tree of fields
      std::vector<Position> fStack;
   public:
      RIterator() { fStack.emplace_back(Position()); }
      RIterator(pointer val, int idxInParent) { fStack.emplace_back(Position(val, idxInParent)); }
      ~RIterator() {}
      /// Given that the iterator points to a valid field which is not the end iterator, go to the next field
      /// in depth-first search order
      void Advance();

      iterator  operator++(int) /* postfix */        { auto r = *this; Advance(); return r; }
      iterator& operator++()    /* prefix */         { Advance(); return *this; }
      reference operator* () const                   { return *fStack.back().fFieldPtr; }
      pointer   operator->() const                   { return fStack.back().fFieldPtr; }
      bool      operator==(const iterator& rh) const { return fStack.back().fFieldPtr == rh.fStack.back().fFieldPtr; }
      bool      operator!=(const iterator& rh) const { return fStack.back().fFieldPtr != rh.fStack.back().fFieldPtr; }
   };

   /// The constructor creates the underlying column objects and connects them to either a sink or a source.
   RTreeFieldBase(std::string_view name, std::string_view type, bool isSimple);
   RTreeFieldBase(const RTreeFieldBase&) = delete;
   RTreeFieldBase& operator =(const RTreeFieldBase&) = delete;
   virtual ~RTreeFieldBase();

   /// Registeres (or re-registers) the backing columns with the physical storage
   void ConnectColumns(Detail::RPageStorage *pageStorage);
   /// Returns the number of columns generated to store data for the field; defaults to 1
   virtual unsigned int GetNColumns() const = 0;

   /// Generates a tree value of the field type.
   virtual RTreeValueBase* GenerateValue() = 0;

   /// Write the given value to a tree. The value object has to be of the same type as the field.
   void Append(const RTreeValueBase &value) {
     if (!fIsSimple) {
        DoAppend(value);
        return;
     }
     fPrincipalColumn->Append(value.fMappedElement);
   }

   /// Populate a single value with data from the tree, which needs to be of the fitting type.
   /// Reading copies data into the memory wrapped by the tree value.
   void Read(TreeIndex_t index, RTreeValueBase* value) {
      if (!fIsSimple) {
         DoRead(index, value);
         return;
      }
      printf("Simple reading index %lu in field %s\n", index, fName.c_str());
      fPrincipalColumn->Read(index, &value->fMappedElement);
   }

   /// Type unsafe bulk read interface; dst must point to a vector of objects of the field type.
   /// TODO(jblomer): can this be type safe?
   void ReadV(TreeIndex_t index, TreeIndex_t count, void *dst)
   {
      if (!fIsSimple) {
         DoReadV(index, count, dst);
         return;
      }
      fPrincipalColumn->ReadV(index, count, dst);
   }

   /// Only for simple types, let the pointer wrapped by the tree value simply point into the page buffer.
   /// The resulting tree value may only be used for as long as no request to another item of this field is made
   /// because another index might trigger a swap of the page buffer.
   /// The dst location must be an object of the field type.
   void Map(TreeIndex_t /*index*/, void** /*dst*/) {
      if (!fIsSimple) {
         // TODO(jblomer)
      }
      //fPrincipalColumn->Map(index, dst);
   }

   /// The number of elements in the principal column. For top level fields, the number of entries.
   TreeIndex_t GetNItems();

   /// Ensure that all received items are written from page buffers to the storage.
   void Flush() const;

   void Attach(std::unique_ptr<Detail::RTreeFieldBase> child);

   std::string GetName() const { return fName; }
   /// Get the tail of the field name up to the last dot
   std::string GetLeafName() const;
   std::string GetType() const { return fType; }
   const RTreeFieldBase* GetParent() const { return fParent; }

   RIterator begin();
   RIterator end();
};

} // namespace Detail

/// The container field for a tree model, which itself has no physical representation
class RTreeFieldRoot : public Detail::RTreeFieldBase {
public:
   RTreeFieldRoot() : Detail::RTreeFieldBase("", "", false /* isSimple */) {}

   void DoGenerateColumns() final;
   unsigned int GetNColumns() const final { return 0; }
   Detail::RTreeValueBase* GenerateValue() final;
};


/// Supported types are implemented as template specializations
template <typename T, typename=void>
class RTreeField : public Detail::RTreeFieldBase {
public:
   RTreeField(std::string_view) : Detail::RTreeFieldBase("", "", false) {
      static_assert(sizeof(T) != sizeof(T), "No I/O support for this type in RForest");
   }
   void DoGenerateColumns() final {}
   unsigned int GetNColumns() const final { return 0; }
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue() final { return nullptr; }
};


/// Implementation for std::vector<ElementT> and other containers that can be iterated from begin() to end().
/// The ContainerT type resolves to a single field (no sub fields) with multiple columns (index plus inner columns).
template <typename ContainerT>
class RTreeField<ContainerT, std::enable_if_t<ROOT::TypeTraits::IsContainer<ContainerT>::value>> : public Detail::RTreeFieldBase {
   using ElementT = typename ContainerT::value_type;
public:
   RTreeField(std::string_view name)
      : Detail::RTreeFieldBase(name, "ROOT::Experimental::TreeIndex_t", false /*isSimple*/)
   {
      std::string elementName(GetName());
      elementName.push_back(kLevelSeparator);
      elementName.append(GetLeafName());
      auto fieldElement = std::make_unique<RTreeField<ElementT>>(elementName);
      Attach(std::move(fieldElement));
   }
   ~RTreeField() = default;

   void DoGenerateColumns() final
   {
      RColumnModel modelIndex(GetName(), EColumnType::kIndex, true /* isSorted*/);
      fColumns.emplace_back(std::make_unique<Detail::RColumn>(modelIndex));
      fPrincipalColumn = fColumns[0].get();
   }

   unsigned int GetNColumns() const final {
      return 1;
   }

   template <typename... ArgsT>
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue(ArgsT&&... args)
   {
      return new ROOT::Experimental::RTreeValue<ContainerT>(this, std::forward<ArgsT>(args)...);
   }
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue() final { return GenerateValue(ContainerT()); }
};

} // namespace Experimental
} // namespace ROOT


template <>
class ROOT::Experimental::RTreeField<float> : public ROOT::Experimental::Detail::RTreeFieldBase {
public:
   explicit RTreeField(std::string_view name) : Detail::RTreeFieldBase(name, "float", true /* isSimple */) {}
   ~RTreeField() = default;

   void DoGenerateColumns() final;
   unsigned int GetNColumns() const final { return 1; }

   template <typename... ArgsT>
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue(ArgsT&&... args)
   {
      auto value = new ROOT::Experimental::RTreeValue<float>(this, std::forward<ArgsT>(args)...);
      value->fMappedElement =
         ROOT::Experimental::Detail::RColumnElement<float, EColumnType::kReal32>(value->Get().get());
      return value;
   }
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue() final { return GenerateValue(0.0); }
};

template <>
class ROOT::Experimental::RTreeField<std::string> : public ROOT::Experimental::Detail::RTreeFieldBase {
private:
   TreeIndex_t fIndex;
   char fChar;
   Detail::RColumnElement<TreeIndex_t, EColumnType::kIndex> fElemIndex;
   Detail::RColumnElement<char, EColumnType::kByte> fElemChar;

protected:
   void DoAppend(const ROOT::Experimental::Detail::RTreeValueBase& value) final;
   void DoRead(ROOT::Experimental::TreeIndex_t index, ROOT::Experimental::Detail::RTreeValueBase* value) final;

public:
   explicit RTreeField(std::string_view name)
      : Detail::RTreeFieldBase(name, "std::string", false /* isSimple */)
      , fElemIndex(&fIndex), fElemChar(&fChar) {}
   ~RTreeField() = default;

   void DoGenerateColumns() final;
   unsigned int GetNColumns() const final { return 2; }

   template <typename... ArgsT>
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue(ArgsT&&... args)
   {
      auto value = new ROOT::Experimental::RTreeValue<std::string>(this, std::forward<ArgsT>(args)...);
      return value;
   }
   ROOT::Experimental::Detail::RTreeValueBase* GenerateValue() final { return GenerateValue(""); }
};

#endif
